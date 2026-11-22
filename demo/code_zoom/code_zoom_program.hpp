#pragma once

// The code-zoom demonstrator's program steps every host's main takes
// (src/host/*/code_zoom_main.*), after the demonstration programs' options
// (program_options.hpp):
//
//   --bench   Draws the window offscreen (no host window) through the
//             zoom sweep (code_bench.hpp: 600 frames from 0.1x to 40x in
//             equal ratios, about the sheet's centre), timing each frame's
//             paint and raster into one reused framebuffer, and prints the
//             report: per band of zooms the frames, the median and 95th
//             percentile milliseconds and the glyphs drawn. --size and
//             --scale set the window (its own size and 1 otherwise).
//             Exit 0, or 1 when no raster could be made or a frame was
//             refused. Taken out of the arguments before
//             parse_program_options sees them (take_code_zoom_arguments).
//   --headless --capture <path.png>
//             Stills at capture_zooms, each about the same content point,
//             written beside <path> as <stem>-<zoom>x.png, one capture line
//             each ({"automation":"capture",...}, as the Gallery's).
//
// Both draw through OffscreenRaster over the bundled faces, as the
// Gallery's headless capture does (gallery_headless_capture.hpp, whose
// helpers they use). Header-only, as program_options.hpp is.

#include "code_bench.hpp"
#include "code_zoom.hpp"
#include "gallery_headless_capture.hpp"
#include "program_options.hpp"

#include "gui_forms/capture/offscreen_raster/offscreen_raster.hpp"
#include "gui_forms/paint_framebuffer.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::code_zoom {

// The stills' zooms, and the content point each is taken about, at the
// sheet's centre: the source's line 62, column 15 (zero-based 61 and 14;
// in the default source, sparkline.cpp, `has_infinity`'s declaration).
inline constexpr std::array<double, 6> capture_zooms{0.1, 0.5, 1.0, 3.0, 12.0, 40.0};
inline constexpr std::size_t capture_line = 61U;
inline constexpr std::uint32_t capture_column = 14U;

// The arguments with --bench taken out (argv[0] kept), for
// parse_program_options; the pointers are argv's.
struct CodeZoomArguments final {
    bool bench{};
    std::vector<char*> passed{};
};

[[nodiscard]] inline CodeZoomArguments take_code_zoom_arguments(int argc, char** argv) {
    CodeZoomArguments arguments{};
    arguments.passed.reserve(argc > 0 ? static_cast<std::size_t>(argc) : 0U);
    for (int index = 0; index < argc; ++index) {
        const std::string_view argument = argv[index] != nullptr ? std::string_view(argv[index]) : std::string_view{};
        if (index > 0 && argument == "--bench") {
            arguments.bench = true;
            continue;
        }
        arguments.passed.push_back(argv[index]);
    }
    return arguments;
}

// `path` with "-<zoom>x" before its extension: shot.png and 0.5 give
// shot-0.5x.png.
[[nodiscard]] inline std::string capture_path_for(const std::string& path, double zoom) {
    const std::filesystem::path original(path);
    std::array<char, 32> label{};
    const int written = std::snprintf(label.data(), label.size(), "-%gx", zoom);
    const std::size_t length = written > 0 ? std::min(static_cast<std::size_t>(written), label.size() - 1U) : 0U;
    std::filesystem::path named = original.parent_path() / original.stem();
    named += std::string(label.data(), length);
    named += original.extension();
    const std::string result = named.string();
    return result;
}

namespace headless {

// An offscreen window ready to paint: active, still, sized, attached to a
// raster, with one framebuffer.
class OffscreenSession final {
public:
    OffscreenSession(Window& window, Size size, double scale) : window_(window), scale_(scale) {
        window_.set_active(true);
        window_.resize(size);
        PresentationOverrides still{};
        const PresentationOverrides* const staged = window_.presentation_overrides();
        if (staged != nullptr) still = *staged;
        still.reduced_motion.set(true);
        still.ambient_motion.set(false);
        window_.set_presentation_overrides(still);
        OffscreenRasterCreateResult made = OffscreenRaster::create(OffscreenRasterOptions{});
        if (!made.succeeded()) {
            failure_ = made.detail();
            return;
        }
        raster_ = made.take_raster();
        attachment_ = std::make_unique<OffscreenAttachment>(*raster_, window_);
        window_.set_scale(scale_);
        window_.perform_layout();
        framebuffer_ = window_.create_framebuffer(window_.client_size(), scale_);
        if (framebuffer_ == nullptr) failure_ = "the raster made no framebuffer";
    }
    OffscreenSession(const OffscreenSession&) = delete;
    OffscreenSession& operator=(const OffscreenSession&) = delete;

    [[nodiscard]] bool ready() const noexcept { return failure_.empty(); }
    [[nodiscard]] const std::string& failure() const& noexcept { return failure_; }
    const std::string& failure() const&& = delete;
    [[nodiscard]] bool paint() { return gallery::detail::paint_headless_frame(window_, *framebuffer_); }
    [[nodiscard]] OffscreenAttachment& attachment() noexcept { return *attachment_; }
    [[nodiscard]] double scale() const noexcept { return scale_; }

private:
    Window& window_;
    double scale_{1.0};
    std::string failure_{};
    // Declared in the order they are made, so the framebuffer, then the
    // attachment (which borrows the raster), then the raster end.
    std::unique_ptr<OffscreenRaster> raster_{};
    std::unique_ptr<OffscreenAttachment> attachment_{};
    std::unique_ptr<PaintFramebuffer> framebuffer_{};
};

[[nodiscard]] inline Size session_size(const demo::ProgramOptions& program) {
    const Size size = program.capture_size.value_or(window_size);
    return size;
}

[[nodiscard]] inline double session_scale(const demo::ProgramOptions& program) {
    const double scale = program.scale_hint > 0.0 ? program.scale_hint : 1.0;
    return scale;
}

// Paints, captures and writes one still; false when any step was refused.
[[nodiscard]] inline bool capture_still(OffscreenSession& session, const std::string& path) {
    if (!session.paint()) {
        std::fprintf(stderr, "code zoom capture: the paint was declined\n");
        return false;
    }
    OffscreenCaptureOptions options{};
    options.scale = session.scale();
    options.include_overlays = true;
    const std::optional<WindowCapture> capture = session.attachment().capture_window(options);
    std::vector<std::byte> png{};
    if (!capture.has_value()) {
        std::fprintf(stderr, "code zoom capture: the window capture was refused\n");
        return false;
    }
    if (!encode_window_capture_png(*capture, png)) {
        std::fprintf(stderr, "code zoom capture: the PNG encoding was refused\n");
        return false;
    }
    if (!gallery::detail::write_file(path, png)) {
        std::fprintf(stderr, "code zoom capture: cannot write %s\n", path.c_str());
        return false;
    }
    return true;
}

} // namespace headless

// --headless --capture: the stills. Exit 0 when every still was written, 1
// otherwise, 2 without --capture.
[[nodiscard]] inline int run_headless_code_zoom(const demo::ProgramOptions& program, Window& window) {
    if (program.capture_path.empty()) {
        std::fprintf(stderr, "--headless needs --capture <path.png>\n");
        demo::print_program_usage("gui-forms-code-zoom");
        return 2;
    }
    const std::shared_ptr<CodeSheet> sheet = find_code_sheet(window);
    if (sheet == nullptr) return 1;
    headless::OffscreenSession session(window, headless::session_size(program), headless::session_scale(program));
    if (!session.ready()) {
        std::fprintf(stderr, "code zoom capture: no raster: %s\n", session.failure().c_str());
        gallery::detail::print_capture_line(false, program.capture_path);
        return 1;
    }
    const Size size = window.client_size();
    const Point centre{size.width * 0.5, size.height * 0.5};
    const Point content = (*sheet).content_point(capture_line, capture_column);
    bool every = true;
    for (const double zoom : capture_zooms) {
        const std::string path = capture_path_for(program.capture_path, zoom);
        (*sheet).show_at(zoom, content, centre);
        const bool saved = headless::capture_still(session, path);
        gallery::detail::print_capture_line(saved, path);
        every = every && saved;
    }
    const int exit_code = every ? 0 : 1;
    return exit_code;
}

// --bench: the sweep and its report.
[[nodiscard]] inline int run_code_zoom_bench(const demo::ProgramOptions& program, Window& window) {
    const std::shared_ptr<CodeSheet> sheet = find_code_sheet(window);
    if (sheet == nullptr) return 1;
    headless::OffscreenSession session(window, headless::session_size(program), headless::session_scale(program));
    if (!session.ready()) {
        std::fprintf(stderr, "code zoom bench: no raster: %s\n", session.failure().c_str());
        return 1;
    }
    (*sheet).set_readout_shown(false);
    const Size size = window.client_size();
    const Point centre{size.width * 0.5, size.height * 0.5};
    const Point content = (*sheet).content_point(capture_line, capture_column);
    std::vector<double> scales{};
    fill_bench_scales(scales, bench_first_zoom, bench_last_zoom, bench_frame_count);
    std::vector<BenchSample> samples{};
    samples.reserve(scales.size());
    // One untimed frame opens the faces.
    (*sheet).show_at(1.0, content, centre);
    if (!session.paint()) return 1;
    for (const double zoom : scales) {
        (*sheet).show_at(zoom, content, centre);
        const FrameTime started = FrameClock::now();
        const bool painted = session.paint();
        const FrameTime ended = FrameClock::now();
        if (!painted) {
            std::fprintf(stderr, "code zoom bench: the frame at %.3fx was declined\n", zoom);
            return 1;
        }
        const double milliseconds = std::chrono::duration<double, std::milli>(ended - started).count();
        samples.push_back(BenchSample{zoom, milliseconds, (*sheet).glyphs_drawn()});
    }
    std::vector<BenchBand> bands{};
    summarize_bench(samples, bench_band_edges, bands);
    const BenchBand whole = summarize_whole(samples, bench_first_zoom, bench_last_zoom);
    const BenchSetting setting{(*sheet).source_name(), size.width, size.height, session.scale()};
    const std::string report = format_bench_report(setting, bands, whole);
    std::fputs(report.c_str(), stdout);
    std::fflush(stdout);
    return 0;
}

} // namespace gui_forms::code_zoom
