// Paragraph layout into caller storage (chat_stress gap A1;
// include/gui_forms/text/paragraph_layout/paragraph_layout.hpp). Against a
// fixed metrics provider whose widths are exact arithmetic: the lines match
// the greedy wrapper TextBlock and DataGridView share, under whole-run
// measurement, for both fits and with kerning that advance sums cannot see;
// collapsed layout paints the lines TextBlock paints, in both directions;
// short storage reports what it needs and changes nothing it should not;
// laying out, measuring and painting allocate nothing once the storage is
// sized (counted); long words overflow or break between clusters, and emoji
// sequences are never split. With a provider's shaping workspace (a fixed
// one here) every line's glyph runs are filled into caller storage at no
// extra shaping under the verified fit, short glyph storage reports the
// exact counts, a workspace of another provider is refused, and
// paint_paragraph draws the runs instead of the text, allocating nothing.
// The bundled engine's numbers are in paragraph_layout_engine_tests.cpp,
// and its glyph runs against reshaped painting in paragraph_glyph_tests.cpp.

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/text/paragraph_layout/paragraph_layout.hpp"
#include "gui_forms/window.hpp"

#include "../src/controls/text_block/line_wrapping.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Counts global allocations while armed, so a sized layout can be shown to
// allocate nothing.
namespace {
bool counting_allocations = false;
std::uint64_t counted_allocations = 0U;
} // namespace

void* operator new(std::size_t size) {
    if (counting_allocations) ++counted_allocations;
    void* allocation = std::malloc(size == 0U ? 1U : size);
    if (allocation == nullptr) throw std::bad_alloc();
    return allocation;
}

void* operator new[](std::size_t size) {
    if (counting_allocations) ++counted_allocations;
    void* allocation = std::malloc(size == 0U ? 1U : size);
    if (allocation == nullptr) throw std::bad_alloc();
    return allocation;
}

void operator delete(void* allocation) noexcept {
    std::free(allocation);
}

void operator delete[](void* allocation) noexcept {
    std::free(allocation);
}

void operator delete(void* allocation, std::size_t) noexcept {
    std::free(allocation);
}

void operator delete[](void* allocation, std::size_t) noexcept {
    std::free(allocation);
}

namespace {

using namespace gui_forms;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(double actual, double expected, const std::string& what) {
    require(std::abs(actual - expected) <= 1.0e-9,
            what + ": got " + std::to_string(actual) + ", expected " + std::to_string(expected));
}

// The scalar values that join the cluster before them in the fixed
// provider: a zero-width joiner, the scalar after one, emoji variation
// selector 16 and the skin-tone modifiers, as a shaper keeps an emoji
// sequence one cluster.
[[nodiscard]] bool joins_previous(char32_t scalar, char32_t previous) noexcept {
    const bool joins = scalar == U'‍' || previous == U'‍' || scalar == U'️' ||
                       (scalar >= U'\U0001F3FB' && scalar <= U'\U0001F3FF');
    return joins;
}

// At 13 px: an emoji cluster (a pictograph or a dingbat) 16 px, i and l 3,
// a space 4, any other scalar 7; joined scalars add nothing.
[[nodiscard]] double scalar_advance(char32_t scalar) noexcept {
    if (scalar >= U'\U0001F000' || (scalar >= U'\u2600' && scalar <= U'\u27BF')) return 16.0;
    if (scalar == U'i' || scalar == U'l') return 3.0;
    if (scalar == U' ') return 4.0;
    return 7.0;
}

// Fills `advances` (one per byte of `text`) with each cluster's advance at
// its first byte; returns their sum. Allocates nothing.
double fill_fixed_advances(std::string_view text, std::span<double> advances) noexcept {
    double sum = 0.0;
    char32_t previous = 0U;
    std::size_t offset = 0U;
    while (offset < text.size()) {
        const Utf8ScalarDecode decoded = decode_utf8_scalar(text, Utf8Offset(offset));
        const double advance = joins_previous(decoded.scalar, previous) ? 0.0 : scalar_advance(decoded.scalar);
        for (std::size_t index = 0U; index < decoded.length; ++index) {
            if (!advances.empty()) advances[offset + index] = index == 0U ? advance : 0.0;
        }
        sum += advance;
        previous = decoded.scalar;
        offset += decoded.length;
    }
    return sum;
}

// The width of a run shaped alone: its clusters' sum, less 2 px for each
// "AV" pair when kerning, which no cluster advance shows.
double fixed_run_width(std::string_view run, bool kerning) noexcept {
    double width = fill_fixed_advances(run, std::span<double>{});
    if (!kerning) return width;
    for (std::size_t index = 0U; index + 1U < run.size(); ++index) {
        if (run[index] == 'A' && run[index + 1U] == 'V') width -= 2.0;
    }
    return width;
}

// A provider with fixed cluster widths and Carlito's design proportions (at
// 13 px a 15 px line box with its baseline at 12). Nothing it does
// allocates; it counts its calls.
class FixedMetrics final : public TextMetricsProvider {
public:
    explicit FixedMetrics(bool kerning) : kerning_(kerning) {}

    ResolvedTextLayout resolve_text_layout_utf8(std::string_view utf8, FontSpec font) override {
        ++resolutions;
        ResolvedTextLayout layout{};
        layout.effective_font = font;
        layout.logical_size = Size{fixed_run_width(utf8, kerning_), font.size * 1.25};
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

    TextExtent measure_text_extent_utf8(std::string_view utf8, FontSpec font) override {
        ++measurements;
        TextExtent extent{};
        extent.logical_size = Size{fixed_run_width(utf8, kerning_), font.size * 1.25};
        extent.ascent = std::ceil(1950.0 / 2048.0 * font.size);
        extent.descent = std::ceil(550.0 / 2048.0 * font.size);
        extent.status = TextResolutionStatus::exact;
        return extent;
    }

    TextResolutionStatus fill_cluster_advances_utf8(std::string_view utf8, FontSpec,
                                                    std::span<double> advances) override {
        ++advance_calls;
        if (advances.size() != utf8.size() || !validate_utf8(utf8).valid()) {
            return TextResolutionStatus::invalid_request;
        }
        static_cast<void>(fill_fixed_advances(utf8, advances));
        return TextResolutionStatus::exact;
    }

    std::size_t resolutions{};
    std::size_t measurements{};
    std::size_t advance_calls{};

private:
    bool kerning_{};
};

// Records each draw_text_utf8 (allocating; not for the counted test).
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

// Counts draw_text_utf8 calls and bytes, allocating nothing.
class CountingPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view text, FontSpec, Color) override {
        ++draws;
        bytes += text.size();
    }
    void draw_image(ImageId, Rect, double) override {}

    std::size_t draws{};
    std::size_t bytes{};
};

// One cluster as the fixed workspace stages it.
struct FixedGlyph final {
    char32_t scalar{};
    std::size_t cluster{};
    double x{};
    double advance{};
};

// The fixed provider's shaping workspace: one glyph a cluster (its first
// scalar's value as the glyph id), one left-to-right run in face 1, pen
// positions the sums of the fixed advances. Reserved once; shaping
// allocates nothing.
class FixedWorkspace final : public ShapingWorkspace {
public:
    explicit FixedWorkspace(std::uint64_t engine_identity) : engine(engine_identity) {}

    std::size_t capacity_bytes() const noexcept override { return staged_.size(); }

    void reserve(std::size_t text_bytes) override {
        if (staged_.size() < text_bytes) staged_.resize(text_bytes);
        shaped_ = false;
    }

    ShapedGlyphCounts shaped_counts() const noexcept override {
        ShapedGlyphCounts counts{};
        if (!shaped_) return counts;
        counts.runs = glyph_count_ != 0U ? 1U : 0U;
        counts.glyphs = glyph_count_;
        return counts;
    }

    GlyphSource glyph_source() const noexcept override {
        GlyphSource source{};
        if (!shaped_) return source;
        source.engine = engine;
        source.generation = 1U;
        return source;
    }

    bool fill_cluster_advances(std::span<double> advances) const noexcept override {
        if (!shaped_ || advances.size() != shaped_bytes_) return false;
        for (double& advance : advances) advance = 0.0;
        for (std::size_t index = 0U; index < glyph_count_; ++index) {
            advances[staged_[index].cluster] += staged_[index].advance;
        }
        return true;
    }

    bool copy_glyphs(Utf8Offset text_offset, std::size_t first_glyph, std::span<GlyphRunRecord> runs,
                     std::span<PositionedGlyph> glyphs) const noexcept override {
        const ShapedGlyphCounts counts = shaped_counts();
        if (!shaped_ || runs.size() < counts.runs || glyphs.size() < counts.glyphs) return false;
        if (counts.runs != 0U) {
            GlyphRunRecord run{};
            run.source_range = Utf8Range{text_offset, Utf8Offset(text_offset.value() + shaped_bytes_)};
            run.face = FontFaceId{1U};
            run.first_glyph = first_glyph;
            run.glyph_count = glyph_count_;
            runs[0] = run;
        }
        for (std::size_t index = 0U; index < glyph_count_; ++index) {
            PositionedGlyph glyph{};
            glyph.glyph = GlyphId{static_cast<std::uint32_t>(staged_[index].scalar)};
            glyph.cluster = Utf8Offset(text_offset.value() + staged_[index].cluster);
            glyph.x = static_cast<float>(staged_[index].x);
            glyphs[index] = glyph;
        }
        return true;
    }

    // Stages `text` (at most capacity_bytes() bytes, valid UTF-8); returns
    // its width.
    double shape(std::string_view text) noexcept {
        glyph_count_ = 0U;
        double pen = 0.0;
        char32_t previous = 0U;
        std::size_t offset = 0U;
        while (offset < text.size()) {
            const Utf8ScalarDecode decoded = decode_utf8_scalar(text, Utf8Offset(offset));
            if (glyph_count_ == 0U || !joins_previous(decoded.scalar, previous)) {
                const double advance = scalar_advance(decoded.scalar);
                staged_[glyph_count_] = FixedGlyph{decoded.scalar, offset, pen, advance};
                ++glyph_count_;
                pen += advance;
            }
            previous = decoded.scalar;
            offset += decoded.length;
        }
        shaped_ = true;
        shaped_bytes_ = text.size();
        return pen;
    }

    const std::uint64_t engine;

private:
    std::vector<FixedGlyph> staged_{};
    std::size_t glyph_count_{};
    std::size_t shaped_bytes_{};
    bool shaped_{};
};

// The fixed provider without kerning, with workspaces of its own engine
// number. It counts its plain calls and its workspace shapings.
class WorkspaceMetrics final : public TextMetricsProvider {
public:
    explicit WorkspaceMetrics(std::uint64_t engine_identity) : engine_(engine_identity) {}

    ResolvedTextLayout resolve_text_layout_utf8(std::string_view utf8, FontSpec font) override {
        ResolvedTextLayout layout{};
        layout.effective_font = font;
        layout.logical_size = Size{fixed_run_width(utf8, false), font.size * 1.25};
        layout.ascent = std::ceil(1950.0 / 2048.0 * font.size);
        layout.descent = std::ceil(550.0 / 2048.0 * font.size);
        layout.status = TextResolutionStatus::exact;
        return layout;
    }

    TextExtent measure_text_extent_utf8(std::string_view utf8, FontSpec font) override {
        ++plain_calls;
        TextExtent extent{};
        extent.logical_size = Size{fixed_run_width(utf8, false), font.size * 1.25};
        extent.status = TextResolutionStatus::exact;
        return extent;
    }

    TextResolutionStatus fill_cluster_advances_utf8(std::string_view utf8, FontSpec,
                                                    std::span<double> advances) override {
        ++plain_calls;
        if (advances.size() != utf8.size() || !validate_utf8(utf8).valid()) {
            return TextResolutionStatus::invalid_request;
        }
        static_cast<void>(fill_fixed_advances(utf8, advances));
        return TextResolutionStatus::exact;
    }

    std::unique_ptr<ShapingWorkspace> create_shaping_workspace() override {
        std::unique_ptr<ShapingWorkspace> workspace = std::make_unique<FixedWorkspace>(engine_);
        return workspace;
    }

    TextExtent shape_in_workspace_utf8(ShapingWorkspace& workspace, std::string_view utf8, FontSpec font) override {
        FixedWorkspace* const own = dynamic_cast<FixedWorkspace*>(&workspace);
        TextExtent extent{};
        extent.status = TextResolutionStatus::invalid_request;
        if (own == nullptr || (*own).engine != engine_ || utf8.size() > (*own).capacity_bytes() ||
            !validate_utf8(utf8).valid()) {
            return extent;
        }
        ++workspace_shapings;
        extent.logical_size = Size{(*own).shape(utf8), font.size * 1.25};
        extent.status = TextResolutionStatus::exact;
        return extent;
    }

    std::size_t plain_calls{};
    std::size_t workspace_shapings{};

private:
    std::uint64_t engine_{};
};

// Records each draw_glyph_runs and draw_text_utf8 (allocating; not for the
// counted test).
class GlyphPainter final : public Painter {
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
        glyph_draws.push_back(false);
        run_counts.push_back(0U);
    }
    void draw_glyph_runs(Point origin, const GlyphRunsView& view, FontSpec, Color) override {
        texts.emplace_back(view.text);
        origins.push_back(origin);
        glyph_draws.push_back(true);
        run_counts.push_back(view.runs.size());
    }
    void draw_image(ImageId, Rect, double) override {}

    std::vector<std::string> texts{};
    std::vector<Point> origins{};
    std::vector<bool> glyph_draws{};
    std::vector<std::size_t> run_counts{};
};

// Counts draw_glyph_runs and draw_text_utf8 calls, allocating nothing.
class CountingGlyphPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override { ++text_draws; }
    void draw_glyph_runs(Point, const GlyphRunsView& view, FontSpec, Color) override {
        ++glyph_draws;
        glyphs += view.glyphs.size();
    }
    void draw_image(ImageId, Rect, double) override {}

    std::size_t text_draws{};
    std::size_t glyph_draws{};
    std::size_t glyphs{};
};

FontSpec carlito() {
    const FontSpec font{FontRole::content, 13.0, 400, false, 0.0};
    return font;
}

ParagraphStyle style_at(TextMetricsProvider& metrics, double width) {
    ParagraphStyle style{};
    style.font = carlito();
    style.line_box = paragraph_line_box(&metrics, style.font, std::nullopt);
    style.width = width;
    return style;
}

// The greedy wrapper's measurement: the provider's whole-run width, counted.
struct ProviderRunWidth final {
    TextMetricsProvider* provider{};
    FontSpec font{};
    std::size_t* calls{};

    double operator()(std::string_view run) const {
        ++(*calls);
        const TextExtent extent = (*provider).measure_text_extent_utf8(run, font);
        return extent.logical_size.width;
    }
};

// TextBlock's and DataGridView's lines for `text` at `width`: the
// estimate's break opportunities (the fixed provider's) and whole-run
// widths.
std::vector<line_wrapping_detail::WrappedLine> reference_lines(TextMetricsProvider& metrics, std::string_view text,
                                                               double width, std::size_t& measurements) {
    std::vector<LineBreakOpportunity> opportunities(text.size(), LineBreakOpportunity::prohibited);
    static_cast<void>(estimate_line_break_opportunities_utf8(text, opportunities));
    const ProviderRunWidth measure{&metrics, carlito(), &measurements};
    std::vector<line_wrapping_detail::WrappedLine> lines;
    line_wrapping_detail::wrap_lines_greedy(text, std::span<const LineBreakOpportunity>(opportunities), width,
                                            measure, lines);
    return lines;
}

std::size_t count_scalars(std::string_view text, std::size_t end) {
    std::size_t scalars = 0U;
    for (std::size_t index = 0U; index < end; ++index) {
        if ((static_cast<unsigned char>(text[index]) & 0xc0U) != 0x80U) ++scalars;
    }
    return scalars;
}

ParagraphMetrics laid_out(const ParagraphLayoutResult& result, const std::string& what) {
    require(result.succeeded(), what + " lays out");
    const ParagraphMetrics metrics = *result.metrics();
    return metrics;
}

const std::array<std::string_view, 6> corpus{
    "aaa bbb cc",
    "The quick brown fox jumps over the lazy dog while the cat sleeps",
    "one\ntwo three four\n\nfive",
    "AVAVAV AVAV AV AVAVAVAV AVA VAVAV",
    "h\xc3\xa9llo w\xc3\xb6rld \xc3\xa7" "a va tr\xc3\xa8s bien",
    "trailing spaces   \nand   inner   runs ",
};

// Each corpus text, at several widths, under whole-run measurement: the
// lines are the greedy wrapper's, byte for byte and width for width, and
// the characters count the scalars before each end.
void test_matches_greedy_reference() {
    const std::array<double, 5> widths{0.0, 30.0, 49.0, 100.0, 1000.0};
    const std::array<bool, 2> kernings{false, true};
    for (const bool kerning : kernings) {
        FixedMetrics metrics(kerning);
        for (const std::string_view text : corpus) {
            for (const double width : widths) {
                const std::string what = std::string(text) + " at " + std::to_string(width) +
                                         (kerning ? " kerned" : "");
                std::size_t reference_measurements = 0U;
                const std::vector<line_wrapping_detail::WrappedLine> expected =
                    reference_lines(metrics, text, width, reference_measurements);
                ParagraphBuffers buffers{};
                buffers.reserve(paragraph_storage_bound(text, ParagraphWhiteSpace::preserve_line_ends));
                const ParagraphStyle style = style_at(metrics, width);
                const ParagraphMetrics result =
                    laid_out(lay_out_paragraph(&metrics, text, style, buffers.storage()), what);
                require(result.line_count == expected.size(), what + ": the greedy wrapper's line count");
                const std::span<const ParagraphLine> lines = buffers.lines(result.line_count);
                for (std::size_t index = 0U; index < expected.size(); ++index) {
                    const std::string line_what = what + " line " + std::to_string(index);
                    require(lines[index].bytes.start.value() == expected[index].begin_byte &&
                                lines[index].bytes.end.value() == expected[index].end_byte,
                            line_what + ": the greedy wrapper's byte range");
                    require_near(lines[index].width, expected[index].width, line_what + ": its width");
                    require(lines[index].characters.start.value() ==
                                    count_scalars(text, expected[index].begin_byte) &&
                                lines[index].characters.end.value() == count_scalars(text, expected[index].end_byte),
                            line_what + ": its characters are scalar positions");
                    require_near(lines[index].baseline, 15.0 * static_cast<double>(index) + 12.0,
                                 line_what + ": its baseline");
                }
                require_near(result.height, 15.0 * static_cast<double>(expected.size()), what + ": the height");
                // The fixed provider takes TextMetricsProvider's default
                // breaks (the white-space estimate), so the status is
                // estimated at every width; the fit is reported apart.
                require(result.status == TextResolutionStatus::estimated,
                        what + ": estimated, from the estimate's break opportunities");
                std::size_t expected_overflow = 0U;
                for (const line_wrapping_detail::WrappedLine& line : expected) {
                    if (line.width > width + 1.0 / 1024.0) ++expected_overflow;
                }
                require(result.overflowing_lines == expected_overflow, what + ": the overflowing lines are counted");
                if (width == 0.0) {
                    for (std::size_t index = 0U; index < expected.size(); ++index) {
                        const ParagraphLine& line = lines[index];
                        const std::string_view shown =
                            text.substr(line.bytes.start.value(), line.bytes.end.value() - line.bytes.start.value());
                        require(shown.find(' ') == std::string_view::npos,
                                what + ": at width 0 each line is one word");
                    }
                }

                // Advance sums are exact without kerning, so the estimated
                // fit gives the same lines with one shaping.
                if (!kerning) {
                    ParagraphStyle estimated = style;
                    estimated.fit = ParagraphFit::estimated;
                    const ParagraphMetrics quick =
                        laid_out(lay_out_paragraph(&metrics, text, estimated, buffers.storage()), what);
                    require(quick.line_count == expected.size(), what + ": estimated, the same line count");
                    require(quick.shaping_calls == 1U, what + ": estimated, one shaping");
                    const std::span<const ParagraphLine> quick_lines = buffers.lines(quick.line_count);
                    for (std::size_t index = 0U; index < expected.size(); ++index) {
                        require(quick_lines[index].bytes.end.value() == expected[index].end_byte,
                                what + ": estimated, the same line ends");
                    }
                }
            }
        }
    }
}

// Kerning narrows "AVAV AVAV" below its advance sum: at its kerned width
// the estimate (sums) breaks it and the verified fit, measuring the run
// whole, keeps it on one line as TextBlock would.
void test_verified_fit_sees_kerning() {
    FixedMetrics metrics(true);
    const std::string_view text = "AVAV AVAV";
    const double kerned = fixed_run_width(text, true);
    require_near(kerned, 8.0 * 7.0 + 4.0 - 4.0 * 2.0, "the kerned width: four AV pairs");
    ParagraphBuffers buffers{};
    buffers.reserve(paragraph_storage_bound(text, ParagraphWhiteSpace::preserve_line_ends));
    ParagraphStyle style = style_at(metrics, kerned);
    const ParagraphMetrics verified = laid_out(lay_out_paragraph(&metrics, text, style, buffers.storage()), "verified");
    require(verified.line_count == 1U, "verified: one line, as whole-run measurement fits it");
    require_near(buffers.lines(1U)[0].width, kerned, "its width is the kerned width Painter draws");
    style.fit = ParagraphFit::estimated;
    const ParagraphMetrics estimated =
        laid_out(lay_out_paragraph(&metrics, text, style, buffers.storage()), "estimated");
    require(estimated.line_count == 2U, "estimated: advance sums break it");
}

// Verified layout of a long paragraph costs about two shapings a line,
// where the greedy wrapper shapes every candidate run.
void test_shaping_cost() {
    FixedMetrics metrics(false);
    std::string text;
    for (std::size_t index = 0U; index < 40U; ++index) text += "word lengths vary a bit ";
    std::size_t reference_measurements = 0U;
    const std::vector<line_wrapping_detail::WrappedLine> expected =
        reference_lines(metrics, text, 200.0, reference_measurements);
    ParagraphBuffers buffers{};
    buffers.reserve(paragraph_storage_bound(text, ParagraphWhiteSpace::preserve_line_ends));
    const ParagraphMetrics result =
        laid_out(lay_out_paragraph(&metrics, text, style_at(metrics, 200.0), buffers.storage()), "long paragraph");
    require(result.line_count == expected.size(), "the long paragraph wraps as the greedy wrapper does");
    require(result.shaping_calls <= 1U + 2U * static_cast<std::uint64_t>(result.line_count),
            "at most the paragraph's shaping and two measurements a line");
    require(result.shaping_calls < static_cast<std::uint64_t>(reference_measurements),
            "fewer shapings than the greedy wrapper's " + std::to_string(reference_measurements));
    require(metrics.advance_calls >= 1U, "the provider's cluster advances are used");
}

// A TextBlock and a collapsed paragraph of the same text, font and width
// paint the same lines at the same origins, in both directions and under
// each alignment.
void test_matches_text_block() {
    FixedMetrics metrics(false);
    const std::array<std::string_view, 3> texts{
        "  aaa   bbb\ncc  dd\teee ffff  ",
        "The quick brown fox jumps over the lazy dog",
        "h\xc3\xa9llo   w\xc3\xb6rld",
    };
    const std::array<double, 3> widths{50.0, 90.0, 400.0};
    const std::array<TextAlignment, 3> alignments{TextAlignment::start, TextAlignment::end, TextAlignment::center};
    const std::array<LayoutDirection, 2> directions{LayoutDirection::left_to_right, LayoutDirection::right_to_left};
    for (const std::string_view text : texts) {
        for (const double width : widths) {
            for (const TextAlignment alignment : alignments) {
                for (const LayoutDirection direction : directions) {
                    const std::string what = std::string(text) + " at " + std::to_string(width);
                    std::shared_ptr<TextBlock> block = make_control<TextBlock>(StableId("paragraph.block"),
                                                                               std::string(text));
                    (*block).font = carlito();
                    (*block).margin = {0.0, 0.0, 0.0, 0.0};
                    (*block).padding = {0.0, 0.0, 0.0, 0.0};
                    (*block).text_align = alignment;
                    (*block).layout_direction = direction;
                    (*block).set_bounds({0.0, 0.0, width, 300.0});
                    const std::unique_ptr<Window> held = std::make_unique<Window>(block, Size{width, 300.0});
                    (*held).set_text_metrics_provider(&metrics);
                    (*held).perform_layout();
                    TextPainter block_painter{};
                    (*block).on_paint(block_painter, Rect{0.0, 0.0, width, 300.0});

                    ParagraphStyle style = style_at(metrics, width);
                    style.font = (*block).text_font((*block).resolved_font());
                    style.line_box = paragraph_line_box(&metrics, style.font, std::nullopt);
                    style.white_space = ParagraphWhiteSpace::collapse;
                    style.alignment = alignment;
                    style.direction = direction == LayoutDirection::right_to_left ? TextDirection::right_to_left
                                                                                  : TextDirection::left_to_right;
                    ParagraphBuffers buffers{};
                    buffers.reserve(paragraph_storage_bound(text, ParagraphWhiteSpace::collapse));
                    const ParagraphMetrics result =
                        laid_out(lay_out_paragraph(&metrics, text, style, buffers.storage()), what);
                    const std::string_view collapsed = buffers.collapsed_text(result.laid_out_bytes);
                    TextPainter paragraph_painter{};
                    ParagraphPaint paint{};
                    paint.visible = Rect{0.0, 0.0, width, 300.0};
                    const std::size_t drawn = paint_paragraph(paragraph_painter, collapsed,
                                                              buffers.lines(result.line_count), style, paint);
                    require(drawn == block_painter.texts.size() &&
                                paragraph_painter.texts.size() == block_painter.texts.size(),
                            what + ": as many lines as TextBlock paints");
                    for (std::size_t index = 0U; index < drawn; ++index) {
                        const std::string line_what = what + " line " + std::to_string(index);
                        require(paragraph_painter.texts[index] == block_painter.texts[index],
                                line_what + ": TextBlock's text \"" + block_painter.texts[index] + "\"");
                        require_near(paragraph_painter.origins[index].x, block_painter.origins[index].x,
                                     line_what + ": TextBlock's x");
                        require_near(paragraph_painter.origins[index].y, block_painter.origins[index].y,
                                     line_what + ": TextBlock's baseline");
                    }
                    const Size measured = (*block).measure_for_layout(
                        LayoutConstraint{definite_extent(LogicalLength{width}), max_content_extent()});
                    require_near(result.height, measured.height, what + ": TextBlock's height");
                }
            }
        }
    }
}

// Short storage: per-byte arrays shorter than the text change nothing and
// report the bound; short line storage keeps the lines it can hold and
// reports the exact count; the requirement then succeeds.
void test_storage_results() {
    FixedMetrics metrics(false);
    const std::string_view text = "aaa bbb ccc ddd eee";
    const ParagraphStyle style = style_at(metrics, 30.0);

    std::array<ParagraphLine, 8> lines{};
    const ParagraphLine sentinel{Utf8Range{Utf8Offset(99U), Utf8Offset(99U)}, ScalarRange{}, Utf8Offset(99U),
                                 -1.0, -1.0, -1.0};
    lines.fill(sentinel);
    std::array<LineBreakOpportunity, 4> short_opportunities{};
    short_opportunities.fill(LineBreakOpportunity::allowed);
    std::array<double, 64> advances{};
    ParagraphStorage storage{};
    storage.lines = std::span<ParagraphLine>(lines);
    storage.opportunities = std::span<LineBreakOpportunity>(short_opportunities);
    storage.advances = std::span<double>(advances);
    const ParagraphLayoutResult short_bytes = lay_out_paragraph(&metrics, text, style, storage);
    require(!short_bytes.succeeded() && short_bytes.required_storage() != nullptr,
            "short per-byte storage is reported");
    const ParagraphStorageRequirement bound{text.size(), 0U, text.size()};
    require(*short_bytes.required_storage() == bound, "with the per-byte size and the line bound");
    require(lines[0] == sentinel && short_opportunities[0] == LineBreakOpportunity::allowed,
            "and nothing was written");
    require(metrics.advance_calls == 0U, "nor shaped");

    std::array<LineBreakOpportunity, 64> opportunities{};
    storage.opportunities = std::span<LineBreakOpportunity>(opportunities);
    storage.lines = std::span<ParagraphLine>(lines).first(2U);
    const ParagraphLayoutResult short_lines = lay_out_paragraph(&metrics, text, style, storage);
    require(short_lines.required_storage() != nullptr, "short line storage is reported");
    require((*short_lines.required_storage()).lines == 5U, "with the exact line count");
    require(lines[0].bytes.end.value() == 3U && lines[1].bytes.start.value() == 4U,
            "and the lines it could hold are written");
    require(lines[2] == sentinel, "and none past its end");

    storage.lines = std::span<ParagraphLine>(lines);
    const ParagraphMetrics full = laid_out(lay_out_paragraph(&metrics, text, style, storage), "the full storage");
    require(full.line_count == 5U, "the requirement's storage succeeds");

    // Collapsed: the requirement is the collapsed size, collapsed_text
    // included.
    const std::string_view spaced = "  a    b  ";
    const ParagraphStorageRequirement collapsed_bound =
        paragraph_storage_bound(spaced, ParagraphWhiteSpace::collapse);
    require(collapsed_bound.per_byte == 3U && collapsed_bound.collapsed_bytes == 3U,
            "the collapsed size bounds collapsed storage");
    ParagraphStyle collapse_style = style;
    collapse_style.white_space = ParagraphWhiteSpace::collapse;
    const ParagraphLayoutResult no_collapsed = lay_out_paragraph(&metrics, spaced, collapse_style, storage);
    require(no_collapsed.required_storage() != nullptr &&
                (*no_collapsed.required_storage()).collapsed_bytes == 3U,
            "collapsing without collapsed_text storage is reported");
    ParagraphBuffers buffers{};
    buffers.reserve(*no_collapsed.required_storage());
    const ParagraphMetrics collapsed =
        laid_out(lay_out_paragraph(&metrics, spaced, collapse_style, buffers.storage()), "collapsed");
    require(buffers.collapsed_text(collapsed.laid_out_bytes) == "a b", "the collapsed text is written");

    // Refusals write nothing.
    lines.fill(sentinel);
    const ParagraphLayoutResult invalid = lay_out_paragraph(&metrics, "\xc3\x28", style, storage);
    require(invalid.error() == ParagraphRequestError::invalid_utf8, "invalid UTF-8 is refused");
    ParagraphStyle bad = style;
    bad.width = -1.0;
    require(lay_out_paragraph(&metrics, text, bad, storage).error() == ParagraphRequestError::invalid_width,
            "a negative width is refused");
    bad.width = std::nan("");
    require(lay_out_paragraph(&metrics, text, bad, storage).error() == ParagraphRequestError::invalid_width,
            "a NaN width is refused");
    bad = style;
    bad.line_box = LineBoxMetrics{};
    require(lay_out_paragraph(&metrics, text, bad, storage).error() == ParagraphRequestError::invalid_line_box,
            "an unprepared line box is refused");
    bad = style;
    bad.font.size = 0.0;
    require(lay_out_paragraph(&metrics, text, bad, storage).error() == ParagraphRequestError::invalid_font,
            "an invalid font is refused");
    require(lines[0] == sentinel, "and nothing was written");

    // An empty text has no line and no height.
    const ParagraphMetrics empty = laid_out(lay_out_paragraph(&metrics, "", style, storage), "empty");
    require(empty.line_count == 0U && empty.height == 0.0, "an empty text has no line box");
}

// Measuring gives lay_out_paragraph's count, height and widest line.
void test_measure_only() {
    FixedMetrics metrics(false);
    ParagraphBuffers buffers{};
    for (const std::string_view text : corpus) {
        buffers.reserve(paragraph_storage_bound(text, ParagraphWhiteSpace::preserve_line_ends));
        const ParagraphStyle style = style_at(metrics, 60.0);
        const ParagraphMetrics full = laid_out(lay_out_paragraph(&metrics, text, style, buffers.storage()), "full");
        ParagraphStorage no_lines = buffers.storage();
        no_lines.lines = std::span<ParagraphLine>{};
        const ParagraphMetrics measured = laid_out(measure_paragraph(&metrics, text, style, no_lines), "measured");
        require(measured.line_count == full.line_count, std::string(text) + ": measuring counts the lines");
        require_near(measured.height, full.height, std::string(text) + ": and their height");
        require_near(measured.widest_line, full.widest_line, std::string(text) + ": and the widest line");
    }
}

// Long words: overflow on their own line, or break between clusters; an
// emoji sequence is never split, and one wider than the line overflows.
void test_long_words_and_emoji() {
    FixedMetrics metrics(false);
    ParagraphBuffers buffers{};
    const std::string_view word = "go supercalifragilistic go";
    buffers.reserve(paragraph_storage_bound(word, ParagraphWhiteSpace::preserve_line_ends));
    ParagraphStyle style = style_at(metrics, 50.0);
    const ParagraphMetrics overflowing = laid_out(lay_out_paragraph(&metrics, word, style, buffers.storage()), "word");
    require(overflowing.line_count == 3U, "normal: the long word takes a line of its own");
    const ParagraphLine long_line = buffers.lines(3U)[1];
    require(long_line.bytes.start.value() == 3U && long_line.bytes.end.value() == 23U, "the whole word");
    require(long_line.width > 50.0, "overflowing the line");

    style.overflow_wrap = ParagraphOverflowWrap::break_word;
    const ParagraphMetrics broken = laid_out(lay_out_paragraph(&metrics, word, style, buffers.storage()), "broken");
    const std::span<const ParagraphLine> pieces = buffers.lines(broken.line_count);
    std::string joined;
    for (const ParagraphLine& piece : pieces) {
        require(piece.width <= 50.0 + 1.0 / 1024.0, "break_word: each piece fits");
        joined += std::string(word.substr(piece.bytes.start.value(), piece.bytes.end.value() - piece.bytes.start.value()));
        joined += '|';
    }
    require(joined == "go|superca|lifragilis|tic go|",
            "the word breaks into the widest fitting pieces, its tail joining the next word: " + joined);

    // Family (man ZWJ woman ZWJ girl: 18 bytes, 5 scalars), thumbs up with
    // a skin tone (8 bytes, 2 scalars) and a heart with VS16 (6 bytes, 2
    // scalars): each one 16 px cluster, run together as one word.
    const std::string family = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7";
    const std::string thumbs = "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD";
    const std::string heart = "\xE2\x9D\xA4\xEF\xB8\x8F";
    const std::string emoji = family + thumbs + heart + family;
    buffers.reserve(paragraph_storage_bound(emoji, ParagraphWhiteSpace::preserve_line_ends));
    ParagraphStyle emoji_style = style_at(metrics, 20.0);
    emoji_style.overflow_wrap = ParagraphOverflowWrap::break_word;
    const ParagraphMetrics clusters =
        laid_out(lay_out_paragraph(&metrics, emoji, emoji_style, buffers.storage()), "emoji");
    require(clusters.line_count == 4U, "one 16 px cluster a line at 20 px");
    const std::span<const ParagraphLine> emoji_lines = buffers.lines(4U);
    const std::array<std::size_t, 5> starts{0U, 18U, 26U, 32U, 50U};
    const std::array<std::size_t, 5> scalar_starts{0U, 5U, 7U, 9U, 14U};
    for (std::size_t index = 0U; index < 4U; ++index) {
        require(emoji_lines[index].bytes.start.value() == starts[index] &&
                    emoji_lines[index].bytes.end.value() == starts[index + 1U],
                "emoji line " + std::to_string(index) + " is one whole cluster");
        require(emoji_lines[index].characters.start.value() == scalar_starts[index] &&
                    emoji_lines[index].characters.end.value() == scalar_starts[index + 1U],
                "emoji line " + std::to_string(index) + " counts its scalars");
        require_near(emoji_lines[index].width, 16.0, "each cluster's width");
    }
    emoji_style.width = 10.0;
    const ParagraphMetrics narrow =
        laid_out(lay_out_paragraph(&metrics, emoji, emoji_style, buffers.storage()), "narrow emoji");
    require(narrow.line_count == 4U && narrow.widest_line == 16.0,
            "a cluster wider than the line overflows on its own, unsplit");
    require(narrow.overflowing_lines == 4U, "and every such line is reported overflowing");
    require(clusters.overflowing_lines == 0U, "at 20 px every cluster fits");
    emoji_style.overflow_wrap = ParagraphOverflowWrap::normal;
    const ParagraphMetrics unbroken =
        laid_out(lay_out_paragraph(&metrics, emoji, emoji_style, buffers.storage()), "unbroken emoji");
    require(unbroken.line_count == 1U && unbroken.widest_line == 64.0,
            "normal: the emoji word overflows whole");
}

// Visible-window painting: only lines whose boxes meet the visible band are
// drawn, at their baselines from the origin.
void test_paint_visible_lines() {
    FixedMetrics metrics(false);
    const std::string_view text = "aaa bbb ccc ddd eee";
    ParagraphBuffers buffers{};
    buffers.reserve(paragraph_storage_bound(text, ParagraphWhiteSpace::preserve_line_ends));
    const ParagraphStyle style = style_at(metrics, 30.0);
    const ParagraphMetrics result = laid_out(lay_out_paragraph(&metrics, text, style, buffers.storage()), "paint");
    TextPainter painter{};
    ParagraphPaint paint{};
    paint.origin = Point{10.0, 100.0};
    paint.visible = Rect{0.0, 100.0 + 16.0, 200.0, 20.0};
    const std::size_t drawn = paint_paragraph(painter, text, buffers.lines(result.line_count), style, paint);
    require(drawn == 2U && painter.texts.size() == 2U, "the band from 16 to 36 px meets the second and third lines");
    require(painter.texts[0] == "bbb" && painter.texts[1] == "ccc", "which are bbb and ccc");
    require_near(painter.origins[0].x, 10.0, "at the origin's x");
    require_near(painter.origins[0].y, 100.0 + 15.0 + 12.0, "on the second baseline");
}

// After the storage is sized, laying out, measuring and painting a
// thousand rows allocate nothing. What is counted: every global operator
// new and new[] in this process while armed, which covers the paragraph
// layout's own code and these test classes. The fixed provider allocates
// nothing, so the bundled engine's shaping is not exercised here; with it,
// HarfBuzzFontEngine::shape and the SkiaRaster kept-layout cache allocate
// per call (and SheenBidi and HarfBuzz call malloc directly, which this
// counter does not see). Zero allocation through the engine is a follow-up.
void test_no_allocations() {
    FixedMetrics metrics(true);
    const std::array<std::string_view, 4> rows{
        "short",
        "a message long enough to wrap over several lines at this width, with AVAV kerning",
        "line one\nline two\n\nline four",
        "supercalifragilisticexpialidocious and \xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD",
    };
    ParagraphBuffers buffers{};
    for (const std::string_view row : rows) {
        buffers.reserve(paragraph_storage_bound(row, ParagraphWhiteSpace::preserve_line_ends));
    }
    ParagraphStyle style = style_at(metrics, 120.0);
    style.overflow_wrap = ParagraphOverflowWrap::break_word;
    ParagraphStyle collapse_style = style;
    collapse_style.white_space = ParagraphWhiteSpace::collapse;
    collapse_style.fit = ParagraphFit::estimated;
    CountingPainter painter{};
    ParagraphPaint paint{};
    paint.visible = Rect{0.0, 0.0, 120.0, 1000.0};
    const ParagraphStorage storage = buffers.storage();
    std::size_t failures = 0U;
    std::size_t lines = 0U;

    counted_allocations = 0U;
    counting_allocations = true;
    for (std::size_t index = 0U; index < 1000U; ++index) {
        const std::string_view row = rows[index % rows.size()];
        const ParagraphLayoutResult result = lay_out_paragraph(&metrics, row, style, storage);
        const ParagraphMetrics* const metrics_of_row = result.metrics();
        if (metrics_of_row == nullptr) {
            ++failures;
            continue;
        }
        lines += (*metrics_of_row).line_count;
        const std::span<const ParagraphLine> row_lines = buffers.lines((*metrics_of_row).line_count);
        static_cast<void>(paint_paragraph(painter, row, row_lines, style, paint));
        if (!measure_paragraph(&metrics, row, collapse_style, storage).succeeded()) ++failures;
    }
    counting_allocations = false;

    require(failures == 0U, "every row lays out and measures");
    require(lines > 1000U && painter.draws > 1000U, "the rows wrap and paint");
    require(counted_allocations == 0U,
            "sized layout, measurement and paint allocate nothing; counted " + std::to_string(counted_allocations));
}

// Glyph output through the fixed workspace: every line's run covers its
// bytes, its glyphs are its clusters at the fixed pen positions from the
// line's origin, the verified fit costs no more shapings than without
// glyphs (and the estimated fit one more a line), and paint_paragraph draws
// each line's runs where it drew its text.
void test_glyph_output() {
    WorkspaceMetrics metrics(77U);
    const std::string_view text = "The quick brown fox jumps over the lazy dog and \xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD";
    ParagraphBuffers buffers{};
    const ParagraphStorageRequirement bound =
        paragraph_storage_bound(text, ParagraphWhiteSpace::preserve_line_ends, ParagraphGlyphOutput::runs);
    require(bound.glyph_runs == text.size() && bound.glyphs == text.size(), "the glyph bound is a byte each");
    buffers.reserve(bound, &metrics);
    require(buffers.storage().workspace != nullptr && (*buffers.storage().workspace).capacity_bytes() >= text.size(),
            "the buffers hold a reserved workspace of the provider's");
    ParagraphStyle plain = style_at(metrics, 70.0);
    ParagraphStyle glyph_style = plain;
    glyph_style.glyph_output = ParagraphGlyphOutput::runs;

    const ParagraphMetrics without = laid_out(lay_out_paragraph(&metrics, text, plain, buffers.storage()), "no glyphs");
    require(!without.glyph_source.valid() && without.glyph_run_count == 0U, "no glyphs without glyph output");
    const std::span<const ParagraphLine> plain_view = buffers.lines(without.line_count);
    const std::vector<ParagraphLine> plain_lines(plain_view.begin(), plain_view.end());

    metrics.plain_calls = 0U;
    const ParagraphMetrics result =
        laid_out(lay_out_paragraph(&metrics, text, glyph_style, buffers.storage()), "glyph output");
    require(metrics.plain_calls == 0U, "every shaping goes through the workspace");
    require(result.glyph_source.engine == 77U && result.glyph_source.generation == 1U, "the workspace's source");
    require(result.line_count == without.line_count && result.line_count > 2U, "the same lines, several");
    require(result.shaping_calls == without.shaping_calls, "glyphs cost no shaping under the verified fit");
    const std::span<const ParagraphLine> lines = buffers.lines(result.line_count);
    const ParagraphGlyphRuns glyphs = buffers.glyph_runs(result);
    require(glyphs.runs.size() == result.glyph_run_count && glyphs.glyphs.size() == result.glyph_count,
            "the buffers give the counts written");
    std::size_t expected_run = 0U;
    std::size_t expected_glyph = 0U;
    for (std::size_t index = 0U; index < lines.size(); ++index) {
        const std::string what = "line " + std::to_string(index);
        const ParagraphLine& line = lines[index];
        require(line.bytes == plain_lines[index].bytes && line.width == plain_lines[index].width,
                what + ": the line laid out without glyphs");
        require(line.first_glyph_run == expected_run && line.glyph_run_count == 1U, what + ": one run, in order");
        const GlyphRunRecord& run = glyphs.runs[line.first_glyph_run];
        require(run.source_range == line.bytes && run.face == FontFaceId{1U} &&
                    run.direction == TextDirection::left_to_right,
                what + ": the run shapes the line's bytes");
        require(run.first_glyph == expected_glyph, what + ": its glyphs follow the previous line's");
        const std::string_view line_text =
            text.substr(line.bytes.start.value(), line.bytes.end.value() - line.bytes.start.value());
        std::vector<double> advances(line_text.size(), 0.0);
        const double line_width = fill_fixed_advances(line_text, advances);
        double pen = 0.0;
        std::size_t glyph_index = run.first_glyph;
        for (std::size_t byte = 0U; byte < line_text.size(); ++byte) {
            if (advances[byte] == 0.0) continue;
            const PositionedGlyph& glyph = glyphs.glyphs[glyph_index];
            require(glyph.cluster.value() == line.bytes.start.value() + byte,
                    what + ": a glyph's cluster in paragraph offsets");
            require_near(static_cast<double>(glyph.x), pen, what + ": its pen position from the line's origin");
            pen += advances[byte];
            ++glyph_index;
        }
        require(glyph_index == run.first_glyph + run.glyph_count, what + ": one glyph a cluster");
        require_near(pen, line_width, what + ": the glyphs span the line");
        expected_run += line.glyph_run_count;
        expected_glyph += run.glyph_count;
    }

    ParagraphPaint paint{};
    paint.origin = Point{10.0, 20.0};
    paint.visible = Rect{0.0, 0.0, 200.0, 400.0};
    TextPainter text_painter{};
    const std::size_t text_drawn = paint_paragraph(text_painter, text, lines, glyph_style, paint);
    GlyphPainter glyph_painter{};
    const std::size_t glyph_drawn = paint_paragraph(glyph_painter, text, lines, glyphs, glyph_style, paint);
    require(text_drawn == glyph_drawn && glyph_drawn == lines.size(), "every line is drawn either way");
    for (std::size_t index = 0U; index < glyph_drawn; ++index) {
        require(glyph_painter.glyph_draws[index] && glyph_painter.run_counts[index] == 1U,
                "each line is drawn from its runs");
        require(glyph_painter.texts[index] == text_painter.texts[index], "with the line's text");
        require_near(glyph_painter.origins[index].x, text_painter.origins[index].x, "at the text's x");
        require_near(glyph_painter.origins[index].y, text_painter.origins[index].y, "on the text's baseline");
    }
    ParagraphGlyphRuns sourceless = glyphs;
    sourceless.source = GlyphSource{};
    GlyphPainter fallback_painter{};
    static_cast<void>(paint_paragraph(fallback_painter, text, lines, sourceless, glyph_style, paint));
    require(!fallback_painter.glyph_draws.empty() && !fallback_painter.glyph_draws[0], "the zero source draws text");

    ParagraphStyle estimated = glyph_style;
    estimated.fit = ParagraphFit::estimated;
    const ParagraphMetrics quick =
        laid_out(lay_out_paragraph(&metrics, text, estimated, buffers.storage()), "estimated glyph output");
    require(quick.shaping_calls == 1U + static_cast<std::uint64_t>(quick.line_count),
            "the estimated fit shapes each line once more for its glyphs");
    require(quick.glyph_source.valid(), "and fills them");
}

// Short glyph storage: storage_too_small with the exact runs and glyphs of
// the layout; a workspace of another provider is not used, and the call
// lays out without glyphs through the plain calls.
void test_glyph_storage_and_foreign_workspace() {
    WorkspaceMetrics metrics(91U);
    const std::string_view text = "one two three four five six seven eight nine ten";
    ParagraphStyle style = style_at(metrics, 50.0);
    style.glyph_output = ParagraphGlyphOutput::runs;
    ParagraphBuffers roomy{};
    roomy.reserve(paragraph_storage_bound(text, ParagraphWhiteSpace::preserve_line_ends, ParagraphGlyphOutput::runs),
                  &metrics);
    const ParagraphMetrics full = laid_out(lay_out_paragraph(&metrics, text, style, roomy.storage()), "roomy");

    ParagraphBuffers tight{};
    ParagraphStorageRequirement small =
        paragraph_storage_bound(text, ParagraphWhiteSpace::preserve_line_ends, ParagraphGlyphOutput::none);
    small.glyph_runs = 1U;
    small.glyphs = 3U;
    tight.reserve(small, &metrics);
    const ParagraphLayoutResult short_result = lay_out_paragraph(&metrics, text, style, tight.storage());
    require(!short_result.succeeded() && short_result.required_storage() != nullptr, "short glyph storage is reported");
    const ParagraphStorageRequirement needed = *short_result.required_storage();
    require(needed.glyph_runs == full.glyph_run_count && needed.glyphs == full.glyph_count,
            "with the exact runs and glyphs");
    require(needed.lines == full.line_count, "and the exact lines");
    tight.reserve(needed, &metrics);
    const ParagraphMetrics retried = laid_out(lay_out_paragraph(&metrics, text, style, tight.storage()), "retried");
    require(retried.glyph_count == full.glyph_count, "storage of that size succeeds");

    WorkspaceMetrics other(92U);
    other.plain_calls = 0U;
    const ParagraphMetrics foreign =
        laid_out(lay_out_paragraph(&other, text, style, roomy.storage()), "another provider's workspace");
    require(!foreign.glyph_source.valid() && foreign.glyph_count == 0U, "lays out without glyphs");
    require(other.workspace_shapings == 0U && other.plain_calls != 0U, "through the provider's plain calls");
    require(foreign.line_count == full.line_count, "with the same lines");
}

// With a reserved workspace and glyph output, a thousand rows lay out and
// paint from their glyph runs allocating nothing (counted as
// test_no_allocations counts: every global operator new while armed).
void test_glyph_output_allocates_nothing() {
    WorkspaceMetrics metrics(93U);
    const std::array<std::string_view, 3> rows{
        "short",
        "a message long enough to wrap over several lines at this width",
        "line one\nline two \xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD",
    };
    ParagraphBuffers buffers{};
    for (const std::string_view row : rows) {
        buffers.reserve(
            paragraph_storage_bound(row, ParagraphWhiteSpace::preserve_line_ends, ParagraphGlyphOutput::runs),
            &metrics);
    }
    ParagraphStyle style = style_at(metrics, 120.0);
    style.glyph_output = ParagraphGlyphOutput::runs;
    CountingGlyphPainter painter{};
    ParagraphPaint paint{};
    paint.visible = Rect{0.0, 0.0, 120.0, 1000.0};
    const ParagraphStorage storage = buffers.storage();
    std::size_t failures = 0U;

    counted_allocations = 0U;
    counting_allocations = true;
    for (std::size_t index = 0U; index < 1000U; ++index) {
        const std::string_view row = rows[index % rows.size()];
        const ParagraphLayoutResult result = lay_out_paragraph(&metrics, row, style, storage);
        const ParagraphMetrics* const row_metrics = result.metrics();
        if (row_metrics == nullptr) {
            ++failures;
            continue;
        }
        const std::span<const ParagraphLine> row_lines = buffers.lines((*row_metrics).line_count);
        static_cast<void>(paint_paragraph(painter, row, row_lines, buffers.glyph_runs(*row_metrics), style, paint));
    }
    counting_allocations = false;

    require(failures == 0U, "every row lays out");
    require(painter.glyph_draws > 1000U && painter.text_draws == 0U, "every line is drawn from its glyph runs");
    require(counted_allocations == 0U,
            "sized glyph output and its paint allocate nothing; counted " + std::to_string(counted_allocations));
}

} // namespace

int main() {
    try {
        test_matches_greedy_reference();
        test_verified_fit_sees_kerning();
        test_shaping_cost();
        test_matches_text_block();
        test_storage_results();
        test_measure_only();
        test_long_words_and_emoji();
        test_paint_visible_lines();
        test_no_allocations();
        test_glyph_output();
        test_glyph_storage_and_foreign_workspace();
        test_glyph_output_allocates_nothing();
        std::cout << "paragraph-layout-tests: pass\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "paragraph-layout-tests: fail: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
