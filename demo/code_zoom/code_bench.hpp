#pragma once

// The code-zoom bench's arithmetic (`--bench`, code_zoom_program.hpp runs
// it): the scripted sweep's scales, the per-band summary of the frame
// times measured at them, and the report printed. Pure, so the report's
// shape is tested without a raster.
//
// The sweep: bench_frame_count frames from bench_first_zoom to
// bench_last_zoom in equal ratios, so every frame draws at a size no frame
// before it drew at, as a real zoom does, and no cache keyed by size helps
// beyond what one frame's own reuse gives.
//
// The report: a header line (ending with the window's display refresh and
// where it came from: "unknown (assumed)" offscreen, where no host reports
// one), a column line, then one line per band of
// scales (bench_band_edges: [0.1, 0.2), ... [20, 40]) holding the frames in
// it, the median and 95th-percentile milliseconds and the mean glyphs drawn
// per frame, then a line for the whole sweep. Percentiles are nearest-rank
// (the value at rank ceil(p * n) of the sorted times), so each is a time
// some frame took. Columns are separated by spaces; numbers are plain
// decimals, milliseconds to three places.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::code_zoom {

inline constexpr std::size_t bench_frame_count = 600U;
inline constexpr double bench_first_zoom = 0.1;
inline constexpr double bench_last_zoom = 40.0;
inline constexpr std::array<double, 10> bench_band_edges{0.1, 0.2, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 40.0};

// One measured frame.
struct BenchSample final {
    double zoom{};
    double milliseconds{};
    std::uint64_t glyphs{};
};

// One band of the report, [zoom_from, zoom_to) (the last band includes its
// end).
struct BenchBand final {
    double zoom_from{};
    double zoom_to{};
    std::size_t frames{};
    double median_milliseconds{};
    double p95_milliseconds{};
    std::uint64_t mean_glyphs{};
};

// What the header line names.
struct BenchSetting final {
    std::string_view file{};
    double width{};
    double height{};
    double scale{};
    // The window's display refresh (DisplayTiming::refresh_hertz), 0 when
    // unknown, and its source's name (display_timing_source_name).
    double refresh_hertz{};
    std::string_view refresh_source{"assumed"};
};

// Replaces `scales` with `frames` zooms from `first` to `last` in equal
// ratios (first and last exact); one frame gives `first`, none nothing.
void fill_bench_scales(std::vector<double>& scales, double first, double last, std::size_t frames);

// The value at nearest rank ceil(fraction * n) of `sorted` (ascending); 0
// for none.
[[nodiscard]] double nearest_rank(std::span<const double> sorted, double fraction) noexcept;

// Replaces `bands` with one band per pair of adjacent `edges` (ascending),
// summarizing the samples whose zoom falls in it; a band no sample falls in
// is kept with zero frames and zero times. Samples outside the edges count
// in no band.
void summarize_bench(std::span<const BenchSample> samples, std::span<const double> edges,
                     std::vector<BenchBand>& bands);

// The whole sweep as one band from the first edge to the last.
[[nodiscard]] BenchBand summarize_whole(std::span<const BenchSample> samples, double zoom_from, double zoom_to);

// The report: the header, the column line, the bands' lines and the whole
// sweep's line, each ending in a newline.
[[nodiscard]] std::string format_bench_report(const BenchSetting& setting, std::span<const BenchBand> bands,
                                              const BenchBand& whole);

} // namespace gui_forms::code_zoom
