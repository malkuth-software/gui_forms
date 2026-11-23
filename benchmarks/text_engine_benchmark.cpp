// Shared text engine cost benchmark (ADR-004). Measurement only.
//
// Workload: the Windows caption benchmark (windows_text_tests --benchmark).
// 100 labels "File Manager folder N", content role, Carlito 13 px, five
// frames into a 1340x850 BGRA surface at scale 1, labels at
// (N mod 5 * 260, N div 5 * 35 + 20). Each frame resolves and draws every
// label as the Windows painter's immediate path does: resolve, shape,
// rasterize, composite. The stage table times each stage alone over the same
// calls. The retained row shapes and rasterizes each label once and then only
// composites, as retained control text does.
//
// Usage: gui_forms_text_engine_benchmark <font-directory> [passes]

#include "text_shape_engine.hpp"
#include "../src/render/text/glyph_run/glyph_run_raster.hpp"
#include "../src/core/text/composite/text_composite.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using gui_forms::Color;
using gui_forms::FontRole;
using gui_forms::FontSpec;
namespace text = gui_forms::render::text;
namespace detail = gui_forms::detail;
using Clock = std::chrono::steady_clock;

constexpr std::uint32_t surface_width = 1340U;
constexpr std::uint32_t surface_height = 850U;
constexpr int label_count = 100;
constexpr int frame_count = 5;

void require(const bool condition, const char* const message) {
    if (condition) return;
    std::fprintf(stderr, "%s\n", message);
    std::exit(1);
}

[[nodiscard]] double milliseconds_since(const Clock::time_point started) {
    const double result = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    return result;
}

struct Label final {
    std::string text;
    double x{};
    double y{};
};

// Stage totals in milliseconds over one pass of every frame.
struct StageTimes final {
    double resolve{};
    double shape{};
    double rasterize{};
    double composite{};
};

struct Surface final {
    std::vector<std::uint32_t> pixels;

    [[nodiscard]] detail::TextCompositeTarget target() {
        detail::TextCompositeTarget result{};
        result.pixels = pixels;
        result.width = surface_width;
        result.height = surface_height;
        result.stride = surface_width;
        result.opaque = true;
        return result;
    }
};

// One label's shaping and coverage, as a retained draw cache keeps them.
struct RetainedLabel final {
    text::ShapedText shaped;
    text::GlyphRunOrigin origin{};
    text::GlyphRunCoverage coverage;
};

[[nodiscard]] detail::TextCoverage placed(const text::GlyphRunCoverage& coverage, const text::GlyphRunOrigin origin) {
    detail::TextCoverage result{};
    result.pixels = coverage.pixels;
    result.width = coverage.width;
    result.height = coverage.height;
    result.left = origin.pixel_x + coverage.left;
    result.top = origin.pixel_y + coverage.top;
    return result;
}

[[nodiscard]] detail::TextCompositeClip whole_surface() {
    detail::TextCompositeClip result{};
    result.rect = {0.0, 0.0, static_cast<double>(surface_width), static_cast<double>(surface_height)};
    return result;
}

class Benchmark final {
public:
    explicit Benchmark(const std::filesystem::path& fonts) : rasterizer_(engine_) {
        std::ifstream stream(fonts / "Carlito-Regular.ttf", std::ios::binary);
        const std::vector<char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        require(!bytes.empty(), "Carlito-Regular.ttf is readable");
        const std::span<const std::byte> encoded(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size());
        require(engine_.register_typeface(FontRole::content, 400U, false, encoded).has_value(), "font registered");
        surface_.pixels.assign(static_cast<std::size_t>(surface_width) * surface_height, 0xFFFFFFFFU);
        labels_.reserve(label_count);
        for (int index = 0; index < label_count; ++index) {
            labels_.push_back(Label{"File Manager folder " + std::to_string(index), static_cast<double>(index % 5 * 260),
                                    static_cast<double>(index / 5 * 35 + 20)});
        }
    }

    // The immediate path, untimed per stage: milliseconds per frame.
    [[nodiscard]] double immediate_frames() {
        const Clock::time_point started = Clock::now();
        for (int frame = 0; frame < frame_count; ++frame) {
            for (const Label& label : labels_) {
                const gui_forms::ResolvedTextLayout layout = engine_.resolve(label.text, font_);
                require(layout.status == gui_forms::TextResolutionStatus::exact, "label resolves");
                draw(label);
            }
        }
        const double result = milliseconds_since(started) / frame_count;
        return result;
    }

    // The same calls, each stage timed alone: milliseconds per frame.
    [[nodiscard]] StageTimes stage_frames() {
        StageTimes totals{};
        for (int frame = 0; frame < frame_count; ++frame) {
            for (const Label& label : labels_) {
                Clock::time_point started = Clock::now();
                const gui_forms::ResolvedTextLayout layout = engine_.resolve(label.text, font_);
                totals.resolve += milliseconds_since(started);
                require(layout.status == gui_forms::TextResolutionStatus::exact, "label resolves");
                started = Clock::now();
                const text::ShapedText shaped = engine_.shape(label.text, font_);
                totals.shape += milliseconds_since(started);
                const text::GlyphRunOrigin origin = text::glyph_run_origin(label.x, label.y);
                started = Clock::now();
                const text::GlyphRunCoverage coverage = rasterizer_.rasterize(shaped, font_, 1.0, origin.phase_x);
                totals.rasterize += milliseconds_since(started);
                started = Clock::now();
                static_cast<void>(detail::composite_text_coverage(placed(coverage, origin), surface_.target(),
                                                                  whole_surface(), ink_));
                totals.composite += milliseconds_since(started);
            }
        }
        totals.resolve /= frame_count;
        totals.shape /= frame_count;
        totals.rasterize /= frame_count;
        totals.composite /= frame_count;
        return totals;
    }

    // Shape and rasterize once, then composite every frame: milliseconds per
    // frame after the first.
    [[nodiscard]] double retained_frames() {
        std::vector<RetainedLabel> retained;
        retained.reserve(labels_.size());
        for (const Label& label : labels_) {
            RetainedLabel entry{};
            entry.shaped = engine_.shape(label.text, font_);
            entry.origin = text::glyph_run_origin(label.x, label.y);
            entry.coverage = rasterizer_.rasterize(entry.shaped, font_, 1.0, entry.origin.phase_x);
            retained.push_back(std::move(entry));
        }
        const Clock::time_point started = Clock::now();
        for (int frame = 1; frame < frame_count; ++frame) {
            for (const RetainedLabel& entry : retained) {
                static_cast<void>(detail::composite_text_coverage(placed(entry.coverage, entry.origin),
                                                                  surface_.target(), whole_surface(), ink_));
            }
        }
        const double result = milliseconds_since(started) / (frame_count - 1);
        return result;
    }

private:
    void draw(const Label& label) {
        const text::ShapedText shaped = engine_.shape(label.text, font_);
        const text::GlyphRunOrigin origin = text::glyph_run_origin(label.x, label.y);
        const text::GlyphRunCoverage coverage = rasterizer_.rasterize(shaped, font_, 1.0, origin.phase_x);
        static_cast<void>(detail::composite_text_coverage(placed(coverage, origin), surface_.target(), whole_surface(), ink_));
    }

    text::TextShapeEngine engine_;
    text::GlyphRunRasterizer rasterizer_;
    Surface surface_;
    std::vector<Label> labels_;
    FontSpec font_{FontRole::content, 13, 400, false, 0.0};
    Color ink_{Color::rgba(20, 30, 40)};
};

[[nodiscard]] double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const double result = values[values.size() / 2U];
    return result;
}

// A decimal count argument: the whole text as an int, else 0 (a value the
// caller refuses). Locale-free, and reports overflow (atoi does neither).
[[nodiscard]] int parsed_count(const std::string_view text) noexcept {
    int value = 0;
    const char* const end = text.data() + text.size();
    const std::from_chars_result parsed = std::from_chars(text.data(), end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        return 0;
    }
    return value;
}

} // namespace

int main(const int argc, char** const argv) {
    require(argc == 2 || argc == 3, "usage: gui_forms_text_engine_benchmark <font-directory> [passes]");
    const int passes = argc == 3 ? std::max(3, parsed_count(argv[2])) : 15;
    const std::filesystem::path fonts(argv[1]);
    Benchmark benchmark(fonts);
    // The first pass fills the engine's and STXlib's lazy state; it is reported
    // apart from the warm median.
    const double first = benchmark.immediate_frames();
    std::vector<double> immediate;
    std::vector<double> resolve;
    std::vector<double> shape;
    std::vector<double> rasterize;
    std::vector<double> composite;
    std::vector<double> retained;
    for (int pass = 0; pass < passes; ++pass) {
        immediate.push_back(benchmark.immediate_frames());
        const StageTimes stages = benchmark.stage_frames();
        resolve.push_back(stages.resolve);
        shape.push_back(stages.shape);
        rasterize.push_back(stages.rasterize);
        composite.push_back(stages.composite);
        retained.push_back(benchmark.retained_frames());
    }
    std::printf("text-engine-benchmark labels=%d frames=%d passes=%d (milliseconds per frame, warm median)\n",
                label_count, frame_count, passes);
    std::printf("immediate first=%.4f warm=%.4f\n", first, median(immediate));
    std::printf("stage resolve=%.4f shape=%.4f rasterize=%.4f composite=%.4f\n", median(resolve), median(shape),
                median(rasterize), median(composite));
    std::printf("retained composite-only=%.4f\n", median(retained));
    return 0;
}
