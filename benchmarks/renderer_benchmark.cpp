#include "gallery.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/metrics/types/metrics_types.hpp"
#include "gui_forms/scheduler/frame_request_token/frame_request_token.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/damage_region/damage_region.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/window.hpp"
#include "skia_raster.hpp"
#include "../src/core/damage/device_damage/device_damage.hpp"

#include <mach/mach.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;
using Clock = std::chrono::steady_clock;
using Nanoseconds = std::chrono::nanoseconds;

constexpr Size gallery_size{900.0, 620.0};
constexpr std::size_t warmup_frames = 24;
constexpr std::size_t measured_frames = 240;
constexpr std::size_t band_frames = 300;
constexpr FrameInterval band_interval = std::chrono::nanoseconds(33'333'333);

struct Distribution final {
    std::uint64_t p50{};
    std::uint64_t p95{};
    std::uint64_t p99{};
    std::uint64_t worst{};
    std::uint64_t mean{};
};

struct ProcessSample final {
    std::uint64_t cpu_nanoseconds{};
    std::uint64_t resident_bytes{};
};

struct PixelDifference final {
    std::uint64_t pixels{};
    std::uint64_t channels{};
    std::uint8_t maximum_channel_delta{};
};

std::uint64_t timeval_nanoseconds(timeval value) {
    return static_cast<std::uint64_t>(value.tv_sec) * 1'000'000'000ULL +
           static_cast<std::uint64_t>(value.tv_usec) * 1'000ULL;
}

ProcessSample process_sample() {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        throw std::runtime_error("getrusage failed");
    }
    mach_task_basic_info_data_t task{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&task), &count) != KERN_SUCCESS) {
        throw std::runtime_error("task_info failed");
    }
    return {timeval_nanoseconds(usage.ru_utime) +
                timeval_nanoseconds(usage.ru_stime),
            static_cast<std::uint64_t>(task.resident_size)};
}

Distribution distribution(std::vector<std::uint64_t> values) {
    if (values.empty()) {
        return {};
    }
    std::sort(values.begin(), values.end());
    const auto percentile = [&values](double fraction) {
        const double index = std::ceil(fraction * static_cast<double>(values.size())) - 1.0;
        return values[std::min(values.size() - 1,
                               static_cast<std::size_t>(std::max(0.0, index)))];
    };
    std::uint64_t sum = 0;
    for (const std::uint64_t value : values) {
        sum += value;
    }
    return {percentile(0.50), percentile(0.95), percentile(0.99), values.back(),
            sum / values.size()};
}

std::uint64_t checksum(std::span<const std::byte> bytes) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const std::byte value : bytes) {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= 1099511628211ULL;
    }
    return hash;
}

PixelDifference compare_pixels(std::span<const std::byte> expected,
                               std::span<const std::byte> actual) {
    if (expected.size() != actual.size() || expected.size() % 4U != 0U) {
        throw std::runtime_error("incompatible renderer surfaces");
    }
    PixelDifference difference;
    for (std::size_t offset = 0; offset < expected.size(); offset += 4U) {
        bool pixel_differs = false;
        for (std::size_t channel = 0; channel < 4U; ++channel) {
            const auto expected_value =
                std::to_integer<std::uint8_t>(expected[offset + channel]);
            const auto actual_value =
                std::to_integer<std::uint8_t>(actual[offset + channel]);
            const auto delta = static_cast<std::uint8_t>(
                expected_value > actual_value ? expected_value - actual_value
                                              : actual_value - expected_value);
            if (delta != 0U) {
                pixel_differs = true;
                ++difference.channels;
                difference.maximum_channel_delta =
                    std::max(difference.maximum_channel_delta, delta);
            }
        }
        difference.pixels += pixel_differs;
    }
    return difference;
}

std::vector<std::byte> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("unable to open " + path.string());
    }
    const auto length = input.tellg();
    if (length <= 0) {
        throw std::runtime_error("empty benchmark input " + path.string());
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), length);
    if (!input) {
        throw std::runtime_error("unable to read " + path.string());
    }
    return bytes;
}

std::string sysctl_string(const char* name) {
    std::size_t length = 0;
    if (sysctlbyname(name, nullptr, &length, nullptr, 0) != 0 || length == 0) {
        return "unknown";
    }
    std::string result(length, '\0');
    if (sysctlbyname(name, result.data(), &length, nullptr, 0) != 0) {
        return "unknown";
    }
    while (!result.empty() && result.back() == '\0') {
        result.pop_back();
    }
    return result;
}

std::uint64_t sysctl_u64(const char* name) {
    std::uint64_t value = 0;
    std::size_t length = sizeof(value);
    return sysctlbyname(name, &value, &length, nullptr, 0) == 0 ? value : 0;
}

std::string json_escape(std::string_view text) {
    std::string escaped;
    for (const char character : text) {
        switch (character) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        default: escaped += character; break;
        }
    }
    return escaped;
}

class RecordWriter final {
public:
    explicit RecordWriter(const std::filesystem::path& output)
        : file_(output) {
        if (!file_) {
            throw std::runtime_error("unable to create benchmark record");
        }
    }

    void write(const std::string& record) {
        std::cout << record << '\n';
        file_ << record << '\n';
    }

private:
    std::ofstream file_;
};

void write_ppm(const std::filesystem::path& path,
               const void* pixels,
               std::uint32_t width,
               std::uint32_t height,
               std::size_t row_bytes) {
    const auto* bgra = static_cast<const std::uint8_t*>(pixels);
    if (bgra == nullptr) {
        throw std::runtime_error("renderer exposed no pixels for artifact");
    }
    std::ofstream output(path, std::ios::binary);
    output << "P6\n" << width << ' ' << height << "\n255\n";
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t* row = bgra + static_cast<std::size_t>(y) * row_bytes;
        for (std::uint32_t x = 0; x < width; ++x) {
            // PPM is red, green, blue; the raster is blue, green, red, alpha.
            const std::uint8_t* const pixel = row + static_cast<std::size_t>(x) * 4U;
            const std::array<char, 3> colour{static_cast<char>(pixel[2]), static_cast<char>(pixel[1]),
                                             static_cast<char>(pixel[0])};
            output.write(colour.data(), 3);
        }
    }
}

std::uint64_t copy_damage(const void* source,
                          std::size_t source_row_bytes,
                          std::uint32_t pixel_width,
                          std::uint32_t pixel_height,
                          double scale,
                          Rect logical_damage,
                          std::vector<std::byte>& destination) {
    const std::uint32_t left = static_cast<std::uint32_t>(
        std::clamp(std::floor(logical_damage.x * scale), 0.0,
                   static_cast<double>(pixel_width)));
    const std::uint32_t top = static_cast<std::uint32_t>(
        std::clamp(std::floor(logical_damage.y * scale), 0.0,
                   static_cast<double>(pixel_height)));
    const std::uint32_t right = static_cast<std::uint32_t>(
        std::clamp(std::ceil((logical_damage.x + logical_damage.width) * scale), 0.0,
                   static_cast<double>(pixel_width)));
    const std::uint32_t bottom = static_cast<std::uint32_t>(
        std::clamp(std::ceil((logical_damage.y + logical_damage.height) * scale), 0.0,
                   static_cast<double>(pixel_height)));
    const std::size_t copy_bytes = static_cast<std::size_t>(right - left) * 4U;
    const auto* source_bytes = static_cast<const std::byte*>(source);
    for (std::uint32_t row = top; row < bottom; ++row) {
        const std::size_t offset = static_cast<std::size_t>(row) * source_row_bytes +
                                   static_cast<std::size_t>(left) * 4U;
        std::memcpy(destination.data() + offset, source_bytes + offset, copy_bytes);
    }
    return static_cast<std::uint64_t>(right - left) * (bottom - top);
}

MetricsSnapshot metrics_delta(const MetricsSnapshot& after,
                              const MetricsSnapshot& before) {
    MetricsSnapshot result;
    result.controls_painted = after.controls_painted - before.controls_painted;
    result.paint_nodes_visited = after.paint_nodes_visited - before.paint_nodes_visited;
    result.display_chunks_rebuilt =
        after.display_chunks_rebuilt - before.display_chunks_rebuilt;
    result.display_chunks_reused = after.display_chunks_reused - before.display_chunks_reused;
    result.display_commands_replayed =
        after.display_commands_replayed - before.display_commands_replayed;
    result.paint_invalidations_consumed =
        after.paint_invalidations_consumed - before.paint_invalidations_consumed;
    result.scheduler_wakes = after.scheduler_wakes - before.scheduler_wakes;
    return result;
}

template <typename Raster>
void initialize_raster(Raster& raster,
                       Window& window,
                       double scale,
                       std::span<const std::byte> regular_font,
                       std::span<const std::byte> bold_font) {
    if (!raster.register_typeface(FontRole::control, 400, false, regular_font) ||
        !raster.register_typeface(FontRole::control, 700, false, bold_font) ||
        !raster.resize(window.client_size(), scale) ||
        !raster.synchronize_images(window.image_resources())) {
        throw std::runtime_error("renderer initialization failed");
    }
    window.set_scale(scale);
}

template <typename Raster>
void paint_once(Raster& raster, Window& window, Rect damage, double scale) {
    damage = detail::align_damage_outward(damage, scale);
    DamageRegion region;
    region.add(damage);
    raster.begin_frame(region);
    window.paint(raster, damage);
    raster.end_frame();
}

template <typename Raster>
std::string damage_record(std::string_view renderer,
                          Raster& raster,
                          Window& window,
                          double scale,
                          double damage_percent) {
    const Rect damage{0.0, 0.0, gallery_size.width,
                      gallery_size.height * damage_percent / 100.0};
    window.set_scale(scale);
    raster.resize(gallery_size, scale);
    paint_once(raster, window, {0.0, 0.0, gallery_size.width, gallery_size.height}, scale);
    const auto full_surface = std::span<const std::byte>(
        static_cast<const std::byte*>(raster.pixels()), raster.byte_size());
    const std::vector<std::byte> full_reference(full_surface.begin(), full_surface.end());
    const std::uint64_t reference_checksum = checksum(full_reference);
    for (std::size_t frame = 0; frame < warmup_frames; ++frame) {
        paint_once(raster, window, damage, scale);
    }

    std::vector<std::byte> presented(raster.byte_size());
    std::vector<std::uint64_t> render_samples;
    std::vector<std::uint64_t> copy_samples;
    render_samples.reserve(measured_frames);
    copy_samples.reserve(measured_frames);
    const MetricsSnapshot metrics_before = window.metrics_snapshot();
    const ProcessSample process_before = process_sample();
    std::uint64_t copied_pixels = 0;
    for (std::size_t frame = 0; frame < measured_frames; ++frame) {
        const auto render_started = Clock::now();
        paint_once(raster, window, damage, scale);
        render_samples.push_back(static_cast<std::uint64_t>(
            std::chrono::duration_cast<Nanoseconds>(Clock::now() - render_started).count()));

        const auto copy_started = Clock::now();
        copied_pixels += copy_damage(
            raster.pixels(), raster.row_bytes(), raster.pixel_width(),
            raster.pixel_height(), scale, damage, presented);
        copy_samples.push_back(static_cast<std::uint64_t>(
            std::chrono::duration_cast<Nanoseconds>(Clock::now() - copy_started).count()));
    }
    const ProcessSample process_after = process_sample();
    const MetricsSnapshot metrics = metrics_delta(window.metrics_snapshot(), metrics_before);
    const Distribution render = distribution(std::move(render_samples));
    const Distribution copy = distribution(std::move(copy_samples));
    const auto pixels = std::span<const std::byte>(
        static_cast<const std::byte*>(raster.pixels()), raster.byte_size());
    const std::uint64_t first_checksum = checksum(pixels);
    const PixelDifference damage_difference = compare_pixels(full_reference, pixels);
    paint_once(raster, window, damage, scale);
    const std::uint64_t second_checksum = checksum(pixels);

    std::ostringstream output;
    output << "{\"type\":\"damage\",\"renderer\":\"" << renderer
           << "\",\"scale\":" << scale
           << ",\"damage_percent\":" << damage_percent
           << ",\"frames\":" << measured_frames
           << ",\"render_ns\":{\"p50\":" << render.p50
           << ",\"p95\":" << render.p95 << ",\"p99\":" << render.p99
           << ",\"worst\":" << render.worst << ",\"mean\":" << render.mean
           << "},\"damage_copy_ns\":{\"p50\":" << copy.p50
           << ",\"p95\":" << copy.p95 << ",\"p99\":" << copy.p99
           << ",\"worst\":" << copy.worst << ",\"mean\":" << copy.mean
           << "},\"copied_pixels\":" << copied_pixels
           << ",\"copied_bytes\":" << copied_pixels * 4ULL
           << ",\"cpu_ns\":" << process_after.cpu_nanoseconds - process_before.cpu_nanoseconds
           << ",\"rss_before\":" << process_before.resident_bytes
           << ",\"rss_after\":" << process_after.resident_bytes
           << ",\"controls_painted\":" << metrics.controls_painted
           << ",\"paint_nodes_visited\":" << metrics.paint_nodes_visited
           << ",\"chunks_rebuilt\":" << metrics.display_chunks_rebuilt
           << ",\"chunks_reused\":" << metrics.display_chunks_reused
           << ",\"commands_replayed\":" << metrics.display_commands_replayed
           << ",\"reference_checksum\":" << reference_checksum
           << ",\"checksum\":" << first_checksum
           << ",\"damage_equivalent\":"
           << (damage_difference.pixels == 0 ? "true" : "false")
           << ",\"different_pixels\":" << damage_difference.pixels
           << ",\"different_channels\":" << damage_difference.channels
           << ",\"maximum_channel_delta\":"
           << static_cast<unsigned int>(damage_difference.maximum_channel_delta)
           << ",\"deterministic\":" << (first_checksum == second_checksum ? "true" : "false")
           << '}';
    return output.str();
}

class EmptyControl final : public Control {
public:
    explicit EmptyControl(StableId id) : Control(std::move(id)) {}
};

class BandControl final : public Control {
public:
    explicit BandControl(StableId id) : Control(std::move(id)) {}

    // The band's UI timer target: repaints the whole band each tick.
    struct FrameTick final {
        std::weak_ptr<BandControl> band;
        void operator()(FrameTime now) const;
    };

    void on_paint(Painter& painter, Rect) override {
        ++sequence_;
        const std::uint8_t phase = static_cast<std::uint8_t>(sequence_ % 96U);
        painter.fill_rect({0.0, 0.0, committed_arranged_bounds().width,
                           committed_arranged_bounds().height},
                          Color::rgba(13, static_cast<std::uint8_t>(70U + phase), 130));
        painter.draw_line({0.0, 1.0}, {committed_arranged_bounds().width, 8.0},
                          Color::rgba(89, 216, 230), 1.0);
    }

private:
    std::uint64_t sequence_{};
};

void BandControl::FrameTick::operator()(FrameTime now) const {
    static_cast<void>(now);
    const std::shared_ptr<BandControl> live = band.lock();
    if (live) (*live).invalidate(Dirty::paint);
}

template <typename Raster>
std::string band_record(std::string_view renderer,
                        Raster& raster,
                        std::span<const std::byte> regular_font,
                        std::span<const std::byte> bold_font) {
    auto root = make_control<EmptyControl>(StableId("benchmark.band.root"));
    auto band = make_control<BandControl>(StableId("benchmark.band.surface"));
    auto guard = make_control<EmptyControl>(StableId("benchmark.band.guard"));
    band->set_requested_bounds({0.0, 0.0, 900.0, 10.0});
    guard->set_requested_bounds({0.0, 20.0, 900.0, 80.0});
    root->add_child(band);
    root->add_child(guard);
    Window window(root, {900.0, 100.0});
    initialize_raster(raster, window, 2.0, regular_font, bold_font);
    window.perform_layout();
    static_cast<void>(window.take_damage());
    paint_once(raster, window, {0.0, 0.0, 900.0, 100.0}, 2.0);
    window.reset_activity_metrics();

    const FrameTime origin{};
    // ADR-016: a UI timer that damages the band, as an animated control's own
    // timer would.
    FrameRequestToken active = window.schedule_ui_timer(
        *band, band_interval, origin + band_interval, BandControl::FrameTick{band});
    std::uint64_t timer_ticks = 0;
    std::vector<std::uint64_t> samples;
    samples.reserve(band_frames);
    std::uint64_t frame_budget_misses = 0;
    std::uint64_t damaged_pixels = 0;
    const ProcessSample process_before = process_sample();
    for (std::size_t frame = 1; frame <= band_frames; ++frame) {
        const FramePollResult poll = window.poll_frame_schedule(
            origin + band_interval * static_cast<FrameInterval::rep>(frame));
        if (poll.ui_timer_ticks != 1) {
            throw std::runtime_error("30 Hz band timer did not emit exactly one tick");
        }
        timer_ticks += poll.ui_timer_ticks;
        DamageRegion damage = window.take_damage();
        if (damage.bounds() != Rect{0.0, 0.0, 900.0, 10.0}) {
            throw std::runtime_error("30 Hz band damage escaped the isolated surface");
        }
        const auto started = Clock::now();
        raster.begin_frame(damage);
        window.paint(raster, damage.bounds());
        raster.end_frame();
        const std::uint64_t elapsed = static_cast<std::uint64_t>(
            std::chrono::duration_cast<Nanoseconds>(Clock::now() - started).count());
        samples.push_back(elapsed);
        frame_budget_misses += elapsed > static_cast<std::uint64_t>(band_interval.count());
        damaged_pixels += 900U * 10U * 4U;
    }
    const ProcessSample process_after = process_sample();
    const MetricsSnapshot metrics = window.metrics_snapshot();
    const Distribution render = distribution(std::move(samples));
    const std::uint64_t expected_control_visits = band_frames * 2U;
    const std::uint64_t surrounding_repaints =
        metrics.controls_painted > expected_control_visits
            ? metrics.controls_painted - expected_control_visits
            : 0;
    active.disconnect();

    std::ostringstream output;
    output << "{\"type\":\"bitmap_band_30hz\",\"renderer\":\""
           << renderer << "\",\"scale\":2,\"frames\":" << band_frames
           << ",\"render_ns\":{\"p50\":" << render.p50
           << ",\"p95\":" << render.p95 << ",\"p99\":" << render.p99
           << ",\"worst\":" << render.worst << ",\"mean\":" << render.mean
           << "},\"frame_budget_ns\":" << band_interval.count()
           << ",\"frame_budget_misses\":" << frame_budget_misses
           << ",\"damaged_pixels\":" << damaged_pixels
           << ",\"cpu_ns\":" << process_after.cpu_nanoseconds - process_before.cpu_nanoseconds
           << ",\"rss_before\":" << process_before.resident_bytes
           << ",\"rss_after\":" << process_after.resident_bytes
           << ",\"scheduler_wakes\":" << metrics.scheduler_wakes
           << ",\"ui_timer_ticks\":" << timer_ticks
           << ",\"controls_painted\":" << metrics.controls_painted
           << ",\"surrounding_repaints\":" << surrounding_repaints
           << ",\"chunks_rebuilt\":" << metrics.display_chunks_rebuilt
           << ",\"commands_replayed\":" << metrics.display_commands_replayed
           << '}';
    return output.str();
}

template <typename Raster>
void run_renderer(std::string_view name,
                  Raster& raster,
                  std::span<const std::byte> regular_font,
                  std::span<const std::byte> bold_font,
                  const std::filesystem::path& output_directory,
                  RecordWriter& records) {
    std::unique_ptr<Window> gallery = gui_forms::gallery::make_gallery();
    initialize_raster(raster, *gallery, 1.0, regular_font, bold_font);
    gallery->perform_layout();
    static_cast<void>(gallery->take_damage());

    const ProcessSample first_before = process_sample();
    const auto first_started = Clock::now();
    paint_once(raster, *gallery, {0.0, 0.0, gallery_size.width, gallery_size.height}, 1.0);
    const std::uint64_t first_frame_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<Nanoseconds>(Clock::now() - first_started).count());
    const ProcessSample first_after = process_sample();
    std::ostringstream first;
    first << "{\"type\":\"in_process_first_frame\",\"renderer\":\""
          << name << "\",\"scale\":1,\"duration_ns\":" << first_frame_ns
          << ",\"cpu_ns\":" << first_after.cpu_nanoseconds - first_before.cpu_nanoseconds
          << ",\"rss_before\":" << first_before.resident_bytes
          << ",\"rss_after\":" << first_after.resident_bytes << '}';
    records.write(first.str());

    for (const double scale : {1.0, 2.0, 3.0}) {
        for (const double percent : {1.0, 10.0, 100.0}) {
            records.write(damage_record(name, raster, *gallery, scale, percent));
        }
        if (scale == 2.0) {
            paint_once(raster, *gallery,
                       {0.0, 0.0, gallery_size.width, gallery_size.height}, scale);
            write_ppm(output_directory / (std::string(name) + "-gallery-2x.ppm"),
                      raster.pixels(), raster.pixel_width(), raster.pixel_height(),
                      raster.row_bytes());
        }
    }

    Raster band_raster;
    records.write(band_record(name, band_raster, regular_font, bold_font));
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path output_directory = argc > 1
            ? std::filesystem::path(argv[1])
            : std::filesystem::path("m2d-renderer-results");
        std::filesystem::create_directories(output_directory);
        RecordWriter records(output_directory / "renderer-results.jsonl");

        utsname system{};
        uname(&system);
        std::ostringstream environment;
        environment << "{\"type\":\"environment\",\"revision\":"
                    "\"workspace-uncommitted\",\"compiler\":\""
                    << json_escape(__VERSION__) << "\",\"os\":\""
                    << json_escape(system.sysname) << ' ' << json_escape(system.release)
                    << "\",\"machine\":\"" << json_escape(sysctl_string("hw.model"))
                    << "\",\"cpu\":\"" << json_escape(sysctl_string("machdep.cpu.brand_string"))
                    << "\",\"memory_bytes\":" << sysctl_u64("hw.memsize")
                    << ",\"power_state\":\"AC power; not independently pinned\","
                    "\"profile\":\"sRGB BGRA8 premultiplied\","
                    "\"samples_per_damage_case\":" << measured_frames
                    << ",\"warmups_per_damage_case\":" << warmup_frames
                    << ",\"allocation_metric\":\"unavailable; RSS before/after only\","
                    "\"present_metric\":\"damage-row CPU copy surrogate; not AppKit compositor\"}";
        records.write(environment.str());

        const std::vector<std::byte> regular_font = read_file(GUI_FORMS_BENCH_REGULAR_FONT);
        const std::vector<std::byte> bold_font = read_file(GUI_FORMS_BENCH_BOLD_FONT);

        gui_forms::render::SkiaRaster skia;
        run_renderer("skia-cpu", skia, regular_font, bold_font,
                     output_directory, records);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "renderer benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
