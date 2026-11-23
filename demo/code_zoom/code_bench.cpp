#include "code_bench.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace gui_forms::code_zoom {
namespace {

// The milliseconds and glyph counts of the samples in [from, to) (or
// [from, to] when `closed`), in `times` (replaced, sorted ascending).
struct BandFill final {
    std::size_t frames{};
    std::uint64_t glyphs{};
};

BandFill fill_band_times(std::span<const BenchSample> samples, double from, double to, bool closed,
                         std::vector<double>& times) {
    times.clear();
    BandFill fill{};
    for (const BenchSample& sample : samples) {
        const bool inside = sample.zoom >= from && (sample.zoom < to || (closed && sample.zoom == to));
        if (!inside) continue;
        times.push_back(sample.milliseconds);
        fill.glyphs += sample.glyphs;
        fill.frames += 1U;
    }
    std::sort(times.begin(), times.end());
    return fill;
}

BenchBand band_of(double from, double to, const BandFill& fill, std::span<const double> sorted) {
    BenchBand band{};
    band.zoom_from = from;
    band.zoom_to = to;
    band.frames = fill.frames;
    band.median_milliseconds = nearest_rank(sorted, 0.5);
    band.p95_milliseconds = nearest_rank(sorted, 0.95);
    band.mean_glyphs = fill.frames == 0U ? 0U : fill.glyphs / fill.frames;
    return band;
}

void append_band_line(std::string& report, const char* label, const BenchBand& band) {
    std::array<char, 160> line{};
    const int written = std::snprintf(line.data(), line.size(), "%s %.2f %.2f %zu %.3f %.3f %llu\n", label,
                                      band.zoom_from, band.zoom_to, band.frames, band.median_milliseconds,
                                      band.p95_milliseconds, static_cast<unsigned long long>(band.mean_glyphs));
    if (written <= 0) return;
    const std::size_t length = std::min(static_cast<std::size_t>(written), line.size() - 1U);
    report.append(line.data(), length);
}

} // namespace

void fill_bench_scales(std::vector<double>& scales, const double first, const double last,
                       const std::size_t frames) {
    scales.clear();
    if (frames == 0U) return;
    scales.reserve(frames);
    scales.push_back(first);
    if (frames == 1U) return;
    const double steps = static_cast<double>(frames - 1U);
    const double log_first = std::log(first);
    const double log_ratio = (std::log(last) - log_first) / steps;
    for (std::size_t frame = 1U; frame + 1U < frames; ++frame) {
        const double zoom = std::exp(log_first + log_ratio * static_cast<double>(frame));
        scales.push_back(zoom);
    }
    scales.push_back(last);
}

double nearest_rank(const std::span<const double> sorted, const double fraction) noexcept {
    if (sorted.empty()) return 0.0;
    const double count = static_cast<double>(sorted.size());
    const double rank = std::ceil(std::clamp(fraction, 0.0, 1.0) * count);
    const std::size_t index = rank < 1.0 ? 0U : static_cast<std::size_t>(rank) - 1U;
    const double value = sorted[std::min(index, sorted.size() - 1U)];
    return value;
}

void summarize_bench(const std::span<const BenchSample> samples, const std::span<const double> edges,
                     std::vector<BenchBand>& bands) {
    bands.clear();
    if (edges.size() < 2U) return;
    bands.reserve(edges.size() - 1U);
    std::vector<double> times{};
    times.reserve(samples.size());
    for (std::size_t edge = 0U; edge + 1U < edges.size(); ++edge) {
        const bool last = edge + 2U == edges.size();
        const BandFill fill = fill_band_times(samples, edges[edge], edges[edge + 1U], last, times);
        bands.push_back(band_of(edges[edge], edges[edge + 1U], fill, times));
    }
}

BenchBand summarize_whole(const std::span<const BenchSample> samples, const double zoom_from, const double zoom_to) {
    std::vector<double> times{};
    times.reserve(samples.size());
    const BandFill fill = fill_band_times(samples, zoom_from, zoom_to, true, times);
    const BenchBand band = band_of(zoom_from, zoom_to, fill, times);
    return band;
}

std::string format_bench_report(const BenchSetting& setting, const std::span<const BenchBand> bands,
                                const BenchBand& whole) {
    std::string report{};
    std::array<char, 256> header{};
    const std::string file(setting.file);
    const std::string source(setting.refresh_source);
    const int written = std::snprintf(header.data(), header.size(),
                                      "code_zoom bench: %zu frames, %.2fx to %.2fx, %s, %.0fx%.0f at scale %.2f, ",
                                      whole.frames, whole.zoom_from, whole.zoom_to, file.c_str(), setting.width,
                                      setting.height, setting.scale);
    if (written > 0) report.append(header.data(), std::min(static_cast<std::size_t>(written), header.size() - 1U));
    int refresh_written = 0;
    if (setting.refresh_hertz > 0.0) {
        refresh_written =
            std::snprintf(header.data(), header.size(), "refresh: %.1f Hz (%s)\n", setting.refresh_hertz, source.c_str());
    } else {
        refresh_written = std::snprintf(header.data(), header.size(), "refresh: unknown (%s)\n", source.c_str());
    }
    if (refresh_written > 0) {
        report.append(header.data(), std::min(static_cast<std::size_t>(refresh_written), header.size() - 1U));
    }
    report.append("label zoom_from zoom_to frames median_ms p95_ms glyphs\n");
    for (const BenchBand& band : bands) append_band_line(report, "band", band);
    append_band_line(report, "all", whole);
    return report;
}

} // namespace gui_forms::code_zoom
