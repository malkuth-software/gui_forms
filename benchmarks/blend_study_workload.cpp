// Blend and opaque study workload (presentation and blending programme,
// 2026-11-16). Measurement only: it selects nothing and claims nothing about
// speed by itself.
//
// It drives the Gallery through the private CPU Skia raster adapter, as the
// paint profile workload does (benchmarks/paint_profile_workload.cpp), and
// records for each case the window's blend counters (blend_study.hpp) and,
// in a separate pass with the study off, the frame times and the final
// frame's pixel hash:
//
// - pages: every Gallery page in gallery_page_specs.hpp order, selected,
//   laid out and painted whole at scales 1 and 2, 4 warm-up and 24 measured
//   frames per page and pass;
// - damage: the first page at 900 x 620, scales 1 and 2, damage bands of 1,
//   10 and 100 % of the height from the top, 8 warm-up and 48 measured
//   frames per case and pass.
//
// Counting and timing are separate passes because counting costs about as
// much as painting (blend_study.hpp, "Cost"). The pixel hash is taken from
// the timing pass, so two builds can be compared for identical pixels.
// At scale 1 each page's two heat maps (overdraw depth and blended against
// copied) are written as PNG files over the page's own frame.
//
// The raster loads the bundled faces the Linux host loads, from
// GUI_FORMS_BLEND_STUDY_FONT_DIRECTORY, as the paint profile does.
//
// Each case writes one JSON line (stdout and <output>/blend-study.jsonl).
// tools/blend_study.py builds and runs this and records the environment.
//
// Usage: gui_forms_blend_study_workload <output-directory>

#include "gallery.hpp"
#include "gallery_page_specs.hpp"
#include "gui_forms/diagnostics/blend_study/blend_study.hpp"
#include "gui_forms/types/damage_region/damage_region.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/window/window.hpp"
#include "skia_raster.hpp"
#include "../src/core/damage/device_damage/device_damage.hpp"
#include "../src/core/image/png_writer.hpp"

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

using gui_forms::BlendFrameCounters;
using gui_forms::BlendStudyMode;
using gui_forms::DamageRegion;
using gui_forms::FontRole;
using gui_forms::Rect;
using gui_forms::Size;
using gui_forms::Window;
using gui_forms::render::SkiaRaster;
using Clock = std::chrono::steady_clock;

constexpr Size damage_window_size{900.0, 620.0};
constexpr std::array<double, 2> damage_scales{1.0, 2.0};
constexpr std::array<double, 3> damage_percents{1.0, 10.0, 100.0};
constexpr std::size_t damage_warmup_frames = 8;
constexpr std::size_t damage_measured_frames = 48;
constexpr std::array<double, 2> page_scales{1.0, 2.0};
constexpr std::size_t page_warmup_frames = 4;
constexpr std::size_t page_measured_frames = 24;

struct BundledFace final {
    std::string_view file;
    FontRole role;
    std::uint16_t weight;
    bool italic;
    bool fallback;
};

// As benchmarks/paint_profile_workload.cpp.
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

// FNV-1a over the raster's 8-byte words, then its tail bytes (as the paint
// profile hashes): an identity for the drawn frame.
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

// One case: what was painted, the counters' sum over the measured frames of
// the counting pass, and the timing pass's frame times and final hash.
struct CaseResult final {
    BlendFrameCounters counters{};
    std::uint64_t counted_frames{};
    std::uint64_t median_ns{};
    std::uint64_t maximum_ns{};
    std::uint64_t total_ns{};
    std::uint64_t pixel_hash{};
};

CaseResult measure(SkiaRaster& raster, Window& window, Rect damage, double scale, std::size_t warmup_frames,
                   std::size_t measured_frames) {
    CaseResult result;
    std::size_t frame = 0;
    // Counting pass.
    window.set_blend_study(BlendStudyMode::counters);
    for (frame = 0; frame < warmup_frames; ++frame) paint_once(raster, window, damage, scale);
    window.reset_blend_study();
    for (frame = 0; frame < measured_frames; ++frame) paint_once(raster, window, damage, scale);
    const gui_forms::BlendStudySnapshot counted = window.blend_study_snapshot();
    result.counters = counted.totals;
    result.counted_frames = counted.frames;
    // Timing pass, the study off.
    window.set_blend_study(BlendStudyMode::off);
    for (frame = 0; frame < warmup_frames; ++frame) paint_once(raster, window, damage, scale);
    std::vector<std::uint64_t> samples;
    samples.reserve(measured_frames);
    for (frame = 0; frame < measured_frames; ++frame) {
        const Clock::time_point started = Clock::now();
        paint_once(raster, window, damage, scale);
        const Clock::duration elapsed = Clock::now() - started;
        samples.push_back(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()));
    }
    for (const std::uint64_t value : samples) result.total_ns += value;
    std::sort(samples.begin(), samples.end());
    if (!samples.empty()) {
        result.median_ns = samples[samples.size() / 2U];
        result.maximum_ns = samples.back();
    }
    result.pixel_hash = pixel_hash(raster);
    return result;
}

std::string case_record(std::string_view phase, std::string_view page, Size size, double scale,
                        double damage_percent, std::size_t warmup_frames, std::size_t frames,
                        const CaseResult& result) {
    std::ostringstream line;
    line << "{\"workload\":\"blend-study\",\"renderer\":\"skia-cpu\",\"phase\":\"" << phase << "\",\"page\":\""
         << page << "\",\"width\":" << size.width << ",\"height\":" << size.height << ",\"scale\":" << scale
         << ",\"damage_percent\":" << damage_percent << ",\"warmup_frames\":" << warmup_frames
         << ",\"frames\":" << frames << ",\"counted_frames\":" << result.counted_frames
         << ",\"counters\":" << gui_forms::blend_frame_counters_json(result.counters)
         << ",\"frame_ns\":{\"median\":" << result.median_ns << ",\"maximum\":" << result.maximum_ns
         << ",\"total\":" << result.total_ns << "}"
         << ",\"pixel_hash\":\"" << std::hex << result.pixel_hash << std::dec << "\"}";
    return line.str();
}

void write_record(std::ofstream& records, const std::string& record) {
    std::cout << record << '\n';
    records << record << '\n';
}

// Paints the page once with `mode` and writes its heat map over a copy of
// the frame as <output>/heat-<name>-<page>.png.
void write_heat_map(SkiaRaster& raster, Window& window, Rect whole, BlendStudyMode mode, std::string_view name,
                    std::string_view page, const std::filesystem::path& output) {
    window.set_blend_study(mode);
    paint_once(raster, window, whole, 1.0);
    const std::span<const std::byte> frame(static_cast<const std::byte*>(raster.pixels()), raster.byte_size());
    std::vector<std::byte> copy(frame.begin(), frame.end());
    gui_forms::InvalidationOverlayTarget target{};
    target.pixels = std::span<std::byte>(copy);
    target.width = raster.pixel_width();
    target.height = raster.pixel_height();
    target.row_bytes = raster.row_bytes();
    const gui_forms::DevicePixelRect clip{0, 0, static_cast<std::int32_t>(target.width),
                                         static_cast<std::int32_t>(target.height)};
    gui_forms::compose_blend_heat_map(target, window.blend_heat_map(), clip);
    window.set_blend_study(BlendStudyMode::off);
    gui_forms::image::PngEncodeInput input{};
    input.width = target.width;
    input.height = target.height;
    input.row_bytes = static_cast<std::uint64_t>(target.row_bytes);
    input.pixels = std::span<const std::byte>(copy.data(), copy.size());
    input.order = gui_forms::image::PngChannelOrder::bgra;
    input.alpha = gui_forms::image::PngSourceAlpha::premultiplied;
    std::vector<std::byte> encoded{};
    if (!gui_forms::image::encode_png(input, encoded)) throw std::runtime_error("heat map PNG encoding failed");
    const std::filesystem::path path = output / ("heat-" + std::string(name) + "-" + std::string(page) + ".png");
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    if (!file) throw std::runtime_error("cannot write " + path.string());
}

void run_page_phase(SkiaRaster& raster, Window& window, std::ofstream& records, const std::filesystem::path& output) {
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
            if (!raster.synchronize_images(window.image_resources())) {
                throw std::runtime_error("image synchronization failed");
            }
            const CaseResult result = measure(raster, window, whole, scale, page_warmup_frames, page_measured_frames);
            write_record(records, case_record("pages", spec.id, size, scale, 100.0, page_warmup_frames,
                                              page_measured_frames, result));
            if (scale == 1.0) {
                write_heat_map(raster, window, whole, BlendStudyMode::overdraw_heat_map, "depth", spec.id, output);
                write_heat_map(raster, window, whole, BlendStudyMode::blend_heat_map, "blend", spec.id, output);
            }
        }
    }
}

void run_damage_phase(SkiaRaster& raster, Window& window, std::ofstream& records) {
    const gui_forms::gallery::GalleryPageSpec& first = gui_forms::gallery::gallery_pages.front();
    if (!gui_forms::gallery::select_gallery_page(window, first.id)) {
        throw std::runtime_error("unknown Gallery page " + std::string(first.id));
    }
    for (const double scale : damage_scales) {
        prepare_scale(raster, window, damage_window_size, scale);
        paint_once(raster, window, {0.0, 0.0, damage_window_size.width, damage_window_size.height}, scale);
        for (const double percent : damage_percents) {
            const Rect damage{0.0, 0.0, damage_window_size.width, damage_window_size.height * percent / 100.0};
            const CaseResult result =
                measure(raster, window, damage, scale, damage_warmup_frames, damage_measured_frames);
            write_record(records, case_record("damage", first.id, damage_window_size, scale, percent,
                                              damage_warmup_frames, damage_measured_frames, result));
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: gui_forms_blend_study_workload <output-directory>\n";
        return 2;
    }
    try {
        const std::filesystem::path output(argv[1]);
        std::filesystem::create_directories(output);
        std::ofstream records(output / "blend-study.jsonl");
        if (!records) throw std::runtime_error("cannot create blend-study.jsonl");
        const std::unique_ptr<Window> gallery = gui_forms::gallery::make_gallery();
        SkiaRaster raster;
        register_bundled_faces(raster, GUI_FORMS_BLEND_STUDY_FONT_DIRECTORY);
        run_page_phase(raster, *gallery, records, output);
        run_damage_phase(raster, *gallery, records);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "blend study workload failed: " << error.what() << '\n';
        return 1;
    }
}
