// Paint profile workload (Phase E, E5's dynamic half). Measurement only: it
// selects nothing and claims nothing about speed by itself.
//
// It drives the Gallery through the private CPU Skia raster adapter as it is
// today, so a sampling profiler (perf on Linux, tools/profile_hot_paths.py)
// can record where paint time goes before the stx_skia swap and after it.
// Two phases, both on one Gallery window and one raster:
//
// - damage: M2d's recorded renderer workload (benchmarks/renderer_benchmark.cpp,
//   macOS only): the first page at 900 x 620, scales 1, 2 and 3, damage bands
//   of 1, 10 and 100 % of the height from the top, 24 warm-up and 240
//   measured frames per case.
// - pages: every Gallery page in gallery_page_specs.hpp order, selected,
//   laid out and painted whole at scales 1 and 2, 4 warm-up and 24 measured
//   frames per page.
// - rerecord: the pages at scale 1, every display chunk recorded again
//   each frame.
//
// The raster loads the bundled faces the Linux host loads
// (linux_host.cpp), from GUI_FORMS_PROFILE_FONT_DIRECTORY, so text is shaped
// and drawn as the Linux Gallery draws it.
//
// Each case writes one JSON line (stdout and <output>/paint-profile.jsonl):
// frame-time median and maximum, the window's instrumentation counters over
// the measured frames (MetricsSnapshot deltas), and an FNV-1a hash of the
// final frame's pixels, so a profile can be tied to the output it drew.
//
// Usage: gui_forms_paint_profile_workload <output-directory>

#include "gallery.hpp"
#include "gallery_page_specs.hpp"
#include "gui_forms/gui_forms.hpp"
#include "skia_raster.hpp"
#include "../src/core/damage/device_damage/device_damage.hpp"
#include "../src/core/display/command/display_command.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using gui_forms::DamageRegion;
using gui_forms::FontRole;
using gui_forms::MetricsSnapshot;
using gui_forms::Rect;
using gui_forms::Size;
using gui_forms::Window;
using gui_forms::render::SkiaRaster;
using Clock = std::chrono::steady_clock;

constexpr Size damage_window_size{900.0, 620.0};
constexpr std::array<double, 3> damage_scales{1.0, 2.0, 3.0};
constexpr std::array<double, 3> damage_percents{1.0, 10.0, 100.0};
constexpr std::size_t damage_warmup_frames = 24;
constexpr std::size_t damage_measured_frames = 240;
constexpr std::array<double, 2> page_scales{1.0, 2.0};
constexpr std::size_t page_warmup_frames = 4;
constexpr std::size_t page_measured_frames = 24;

// A bundled face and the role it is registered for; a fallback face serves
// scripts the role faces lack.
struct BundledFace final {
    std::string_view file;
    FontRole role;
    std::uint16_t weight;
    bool italic;
    bool fallback;
};

constexpr std::array<BundledFace, 17> bundled_faces{{
    {"PortsmouthRapids.ttf", FontRole::control, 400, false, false},
    {"PortsmouthRapids-Bold.ttf", FontRole::control, 700, false, false},
    {"Carlito-Regular.ttf", FontRole::content, 400, false, false},
    {"Carlito-Bold.ttf", FontRole::content, 700, false, false},
    {"Carlito-Italic.ttf", FontRole::content, 400, true, false},
    {"Carlito-BoldItalic.ttf", FontRole::content, 700, true, false},
    {"Cousine-Regular.ttf", FontRole::monospace, 400, false, false},
    {"Cousine-Bold.ttf", FontRole::monospace, 700, false, false},
    {"Cousine-Italic.ttf", FontRole::monospace, 400, true, false},
    {"Cousine-BoldItalic.ttf", FontRole::monospace, 700, true, false},
    {"NotoSansCJKjp-Regular.otf", FontRole::content, 400, false, true},
    {"NotoSansArabic-Regular.ttf", FontRole::content, 400, false, true},
    {"NotoSansHebrew-Regular.ttf", FontRole::content, 400, false, true},
    {"NotoSansDevanagari-Regular.ttf", FontRole::content, 400, false, true},
    {"NotoSansBengali-Regular.ttf", FontRole::content, 400, false, true},
    {"NotoSansGurmukhi-Regular.ttf", FontRole::content, 400, false, true},
    {"NotoEmoji-Regular.ttf", FontRole::content, 400, false, true},
}};

// One measured case: the frame times and the counters over its frames.
struct CaseResult final {
    std::uint64_t median_ns = 0;
    std::uint64_t maximum_ns = 0;
    std::uint64_t total_ns = 0;
    MetricsSnapshot counters;
    std::uint64_t pixel_hash = 0;
};

// Every face must load: a missing one would change what the workload draws.
void register_bundled_faces(SkiaRaster& raster, const std::filesystem::path& directory) {
    for (const BundledFace& face : bundled_faces) {
        const std::string path = (directory / face.file).string();
        bool accepted = false;
        if (face.fallback) {
            accepted = raster.register_fallback_typeface_file(face.weight, face.italic, path.c_str());
        } else {
            accepted = raster.register_typeface_file(face.role, face.weight, face.italic, path.c_str());
        }
        if (!accepted) throw std::runtime_error("cannot load bundled face " + path);
    }
}

// FNV-1a over the raster's 8-byte words (host byte order), then its tail
// bytes: an identity for the drawn frame, not a checksum against corruption.
// Word steps keep the hash a small part of the samples (the first run spent
// 6.7 % of them hashing byte by byte).
std::uint64_t pixel_hash(const SkiaRaster& raster) {
    const std::span<const std::byte> bytes(static_cast<const std::byte*>(raster.pixels()), raster.byte_size());
    constexpr std::size_t word_bytes = sizeof(std::uint64_t);
    const std::size_t words = bytes.size() / word_bytes;
    std::uint64_t hash = 14695981039346656037ULL;
    std::size_t index = 0;
    for (index = 0; index < words; ++index) {
        std::uint64_t word = 0;
        std::memcpy(&word, bytes.data() + index * word_bytes, word_bytes);
        hash ^= word;
        hash *= 1099511628211ULL;
    }
    for (index = words * word_bytes; index < bytes.size(); ++index) {
        hash ^= std::to_integer<std::uint64_t>(bytes[index]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

// The counters a paint changes, as after minus before.
MetricsSnapshot counter_delta(const MetricsSnapshot& after, const MetricsSnapshot& before) {
    MetricsSnapshot delta;
    delta.paint_passes = after.paint_passes - before.paint_passes;
    delta.controls_painted = after.controls_painted - before.controls_painted;
    delta.paint_nodes_visited = after.paint_nodes_visited - before.paint_nodes_visited;
    delta.display_chunks_rebuilt = after.display_chunks_rebuilt - before.display_chunks_rebuilt;
    delta.display_chunks_reused = after.display_chunks_reused - before.display_chunks_reused;
    delta.display_commands_replayed = after.display_commands_replayed - before.display_commands_replayed;
    delta.painted_damage_area = after.painted_damage_area - before.painted_damage_area;
    return delta;
}

void paint_once(SkiaRaster& raster, Window& window, Rect damage, double scale) {
    const Rect aligned = gui_forms::detail::align_damage_outward(damage, scale);
    DamageRegion region;
    region.add(aligned);
    raster.begin_frame(region);
    static_cast<void>(window.paint(raster, aligned));
    raster.end_frame();
}

void prepare_scale(SkiaRaster& raster, Window& window, Size size, double scale) {
    window.resize(size);
    window.set_scale(scale);
    window.perform_layout();
    if (!raster.resize(size, scale) || !raster.synchronize_images(window.image_resources())) {
        throw std::runtime_error("raster resize failed");
    }
    static_cast<void>(window.take_damage());
}

// Marks every control's paint dirty, so that the next frame records every
// display chunk again (the rerecord phase).
void dirty_every_chunk(Window& window) {
    const Control::Ptr root = window.root();
    if (root) (*root).invalidate_subtree(Dirty::paint);
    static_cast<void>(window.take_damage());
}

CaseResult measure(SkiaRaster& raster, Window& window, Rect damage, double scale,
                   std::size_t warmup_frames, std::size_t measured_frames, bool rerecord = false) {
    std::size_t frame = 0;
    for (frame = 0; frame < warmup_frames; ++frame) {
        if (rerecord) dirty_every_chunk(window);
        paint_once(raster, window, damage, scale);
    }
    std::vector<std::uint64_t> samples;
    samples.reserve(measured_frames);
    const MetricsSnapshot before = window.metrics_snapshot();
    for (frame = 0; frame < measured_frames; ++frame) {
        if (rerecord) dirty_every_chunk(window);
        const Clock::time_point started = Clock::now();
        paint_once(raster, window, damage, scale);
        const Clock::duration elapsed = Clock::now() - started;
        samples.push_back(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()));
    }
    CaseResult result;
    result.counters = counter_delta(window.metrics_snapshot(), before);
    for (const std::uint64_t value : samples) result.total_ns += value;
    std::sort(samples.begin(), samples.end());
    if (!samples.empty()) {
        result.median_ns = samples[samples.size() / 2U];
        result.maximum_ns = samples.back();
    }
    result.pixel_hash = pixel_hash(raster);
    return result;
}

std::string case_record(std::string_view phase, std::string_view page, double scale, double damage_percent,
                        std::size_t frames, const CaseResult& result) {
    std::ostringstream line;
    line << "{\"workload\":\"paint\",\"renderer\":\"skia-cpu\",\"phase\":\"" << phase
         << "\",\"page\":\"" << page << "\",\"scale\":" << scale
         << ",\"damage_percent\":" << damage_percent << ",\"frames\":" << frames
         << ",\"frame_ns\":{\"median\":" << result.median_ns << ",\"maximum\":" << result.maximum_ns
         << ",\"total\":" << result.total_ns << "}"
         << ",\"counters\":{\"paint_passes\":" << result.counters.paint_passes
         << ",\"controls_painted\":" << result.counters.controls_painted
         << ",\"paint_nodes_visited\":" << result.counters.paint_nodes_visited
         << ",\"display_chunks_rebuilt\":" << result.counters.display_chunks_rebuilt
         << ",\"display_chunks_reused\":" << result.counters.display_chunks_reused
         << ",\"display_commands_replayed\":" << result.counters.display_commands_replayed
         << ",\"painted_damage_area\":" << result.counters.painted_damage_area << "}"
         << ",\"pixel_hash\":\"" << std::hex << result.pixel_hash << std::dec << "\"}";
    return line.str();
}

void write_record(std::ofstream& records, const std::string& record) {
    std::cout << record << '\n';
    records << record << '\n';
}

void run_damage_phase(SkiaRaster& raster, Window& window, std::ofstream& records) {
    const std::string_view page = gui_forms::gallery::selected_gallery_page(window);
    for (const double scale : damage_scales) {
        prepare_scale(raster, window, damage_window_size, scale);
        paint_once(raster, window, {0.0, 0.0, damage_window_size.width, damage_window_size.height}, scale);
        for (const double percent : damage_percents) {
            const Rect damage{0.0, 0.0, damage_window_size.width, damage_window_size.height * percent / 100.0};
            const CaseResult result =
                measure(raster, window, damage, scale, damage_warmup_frames, damage_measured_frames);
            write_record(records, case_record("damage", page, scale, percent, damage_measured_frames, result));
        }
    }
}

void run_page_phase(SkiaRaster& raster, Window& window, std::ofstream& records) {
    const Size size = gui_forms::gallery::gallery_default_size;
    const Rect whole{0.0, 0.0, size.width, size.height};
    for (const double scale : page_scales) {
        prepare_scale(raster, window, size, scale);
        for (const gui_forms::gallery::GalleryPageSpec& spec : gui_forms::gallery::gallery_pages) {
            if (!gui_forms::gallery::select_gallery_page(window, spec.id)) {
                throw std::runtime_error("unknown Gallery page " + std::string(spec.id));
            }
            window.perform_layout();
            static_cast<void>(window.take_damage());
            const CaseResult result = measure(raster, window, whole, scale, page_warmup_frames, page_measured_frames);
            write_record(records, case_record("pages", spec.id, scale, 100.0, page_measured_frames, result));
        }
    }
}

// rerecord: every Gallery page at scale 1, every display chunk recorded
// again each frame (N35's steady-state cost of a themed control: cascade,
// part records and raster hits, with the recording itself).
void run_rerecord_phase(SkiaRaster& raster, Window& window, std::ofstream& records) {
    const Size size = gui_forms::gallery::gallery_default_size;
    const Rect whole{0.0, 0.0, size.width, size.height};
    prepare_scale(raster, window, size, 1.0);
    for (const gui_forms::gallery::GalleryPageSpec& spec : gui_forms::gallery::gallery_pages) {
        if (!gui_forms::gallery::select_gallery_page(window, spec.id)) {
            throw std::runtime_error("unknown Gallery page " + std::string(spec.id));
        }
        window.perform_layout();
        static_cast<void>(window.take_damage());
        const CaseResult result =
            measure(raster, window, whole, 1.0, page_warmup_frames, page_measured_frames, true);
        write_record(records, case_record("rerecord", spec.id, 1.0, 100.0, page_measured_frames, result));
        // S2's memory figure: the page's display commands (replayed once a
        // frame) at their fixed record size, strings and vectors aside.
        const std::uint64_t commands = result.counters.display_commands_replayed / page_measured_frames;
        std::ostringstream line;
        line << "{\"workload\":\"paint\",\"phase\":\"display-memory\",\"page\":\"" << spec.id
             << "\",\"commands\":" << commands << ",\"command_bytes\":" << sizeof(gui_forms::detail::DisplayCommand)
             << ",\"fixed_bytes\":" << commands * sizeof(gui_forms::detail::DisplayCommand) << "}";
        write_record(records, line.str());
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: gui_forms_paint_profile_workload <output-directory>\n";
        return 2;
    }
    try {
        const std::filesystem::path output(argv[1]);
        std::filesystem::create_directories(output);
        std::ofstream records(output / "paint-profile.jsonl");
        if (!records) throw std::runtime_error("cannot create paint-profile.jsonl");
        const std::unique_ptr<Window> gallery = gui_forms::gallery::make_gallery();
        SkiaRaster raster;
        register_bundled_faces(raster, GUI_FORMS_PROFILE_FONT_DIRECTORY);
        run_damage_phase(raster, *gallery, records);
        run_page_phase(raster, *gallery, records);
        run_rerecord_phase(raster, *gallery, records);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "paint profile workload failed: " << error.what() << '\n';
        return 1;
    }
}
