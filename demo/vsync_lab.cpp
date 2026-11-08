#include "vsync_lab.hpp"

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/live_surface.hpp"
#include "gui_forms/range_controls.hpp"
#include "gui_forms/timer.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace gui_forms::vsync_lab {
namespace {

using namespace std::chrono_literals;

// The waterfall is designed in a 1024 x 480 frame (its peaks and grid are
// placed in those coordinates, the spectrum strip is the top 112 rows); the
// producer renders it at the surface's own size.
constexpr std::uint32_t surface_width = 1024U;
constexpr std::uint32_t surface_height = 480U;
constexpr std::uint32_t spectrum_height = 112U;
// The largest surface the producer renders, in device pixels per side.
constexpr std::uint32_t maximum_surface_side = 4096U;
// While the control's size keeps changing (a drag-resize asks for a new
// size on every mouse move), the producer reconfigures at most this often;
// in between the window scales the current frame to the control.
constexpr std::chrono::milliseconds reconfigure_interval{16};

// A surface size in device pixels, packed into one atomic word by the UI
// thread and read by the producer (width in the high half).
struct SurfaceSize final {
    std::uint32_t width{};
    std::uint32_t height{};
};

std::uint64_t pack_surface_size(SurfaceSize size) noexcept {
    const std::uint64_t packed = (static_cast<std::uint64_t>(size.width) << 32U) | size.height;
    return packed;
}

SurfaceSize unpack_surface_size(std::uint64_t packed) noexcept {
    SurfaceSize size;
    size.width = static_cast<std::uint32_t>(packed >> 32U);
    size.height = static_cast<std::uint32_t>(packed & 0xffff'ffffU);
    return size;
}

// Every pixel the producer writes has alpha 255, so the window may copy the
// frame instead of blending it over what lies beneath.
LiveSurfaceDescription waterfall_description(SurfaceSize size) noexcept {
    LiveSurfaceDescription description;
    description.width = size.width;
    description.height = size.height;
    description.opaque = true;
    return description;
}

// One producer frame, for the optional trace (GUI_FORMS_VSYNC_LAB_TRACE).
// Times are steady-clock nanoseconds since the producer started.
struct ProducerTraceEntry final {
    std::uint64_t started{};
    std::uint64_t reconfigure_nanoseconds{};
    std::uint64_t render_nanoseconds{};
    std::uint64_t publish_nanoseconds{};
    // The surface's read acquires after the publish: a presenter's acquire
    // advances it, so the trace shows when frames were taken.
    std::uint64_t read_acquires{};
    std::uint32_t width{};
    std::uint32_t height{};
    bool published{};
};

// The trace holds at most this many frames (about 34 s at 240 Hz),
// reserved before the producer starts.
constexpr std::size_t maximum_trace_entries = 8192U;

std::uint64_t nanoseconds_between(std::chrono::steady_clock::time_point from,
                                  std::chrono::steady_clock::time_point to) noexcept {
    const std::uint64_t nanoseconds =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(to - from).count());
    return nanoseconds;
}

class WaterfallSurface final : public Control {
public:
    struct Snapshot final {
        std::uint64_t producer_frames{};
        std::uint64_t published_frames{};
        std::uint64_t paint_samples{};
        std::uint64_t wake_posts{};
        std::uint64_t last_painted_generation{};
        LiveSurfaceSnapshot surface;
        // The producer is idle because its window cannot present its frames.
        bool presentation_idle{};
    };

    explicit WaterfallSurface(StableId stable_id)
        : Control(std::move(stable_id)),
          surface_(LiveSurface::create(waterfall_description({surface_width, surface_height}))) {
        set_focusable(true);
        accessible_name = "High-rate waterfall and tearing diagnostic";
        const char* const trace_path = std::getenv("GUI_FORMS_VSYNC_LAB_TRACE");
        if (trace_path != nullptr && *trace_path != '\0') {
            trace_path_ = trace_path;
            trace_.reserve(maximum_trace_entries);
        }
        if (surface_) {
            worker_ = std::thread(&WaterfallSurface::producer_loop, this);
        }
    }

    ~WaterfallSurface() override {
        disconnect_surface_wake();
        stop_worker();
        write_trace();
    }

    void set_producer_rate(std::uint32_t frames_per_second) noexcept {
        producer_rate_.store(
            std::clamp<std::uint32_t>(frames_per_second, 15U, 1000U),
            std::memory_order_release);
    }

    void set_rows_per_frame(std::uint32_t rows) noexcept {
        rows_per_frame_.store(std::clamp<std::uint32_t>(rows, 1U, 12U),
                              std::memory_order_release);
    }

    void set_contrast(std::uint32_t contrast) noexcept {
        contrast_.store(std::clamp<std::uint32_t>(contrast, 25U, 250U),
                        std::memory_order_release);
    }

    void set_synthetic_load(std::uint32_t load) noexcept {
        synthetic_load_.store(std::min<std::uint32_t>(load, 100U),
                              std::memory_order_release);
    }

    void set_paused(bool paused) noexcept {
        paused_.store(paused, std::memory_order_release);
    }

    // Whether the window can present the surface (UI thread; the producer
    // idles while it cannot).
    void set_presentable(bool presentable) noexcept {
        presentable_.store(presentable, std::memory_order_release);
    }

    [[nodiscard]] bool paused() const noexcept {
        return paused_.load(std::memory_order_acquire);
    }

    void set_full_window_repaint(bool enabled) noexcept {
        full_window_repaint_.store(enabled, std::memory_order_release);
    }

    [[nodiscard]] Snapshot snapshot() const noexcept {
        Snapshot result;
        result.producer_frames = producer_frames_.load(std::memory_order_acquire);
        result.published_frames = published_frames_.load(std::memory_order_acquire);
        result.paint_samples = paint_samples_.load(std::memory_order_acquire);
        result.wake_posts = wake_posts_.load(std::memory_order_acquire);
        result.last_painted_generation =
            last_painted_generation_.load(std::memory_order_acquire);
        if (surface_) result.surface = (*surface_).snapshot();
        result.presentation_idle = !presentable_.load(std::memory_order_acquire);
        return result;
    }

    void on_paint(Painter& painter, Rect) override {
        const Rect bounds = client_rectangle();
        const Rect image = bounds;
        if (surface_ && !image.empty()) {
            painter.draw_live_surface(surface_, image, 1.0);
        }
    }

    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override {
        SemanticDescriptor descriptor;
        descriptor.role = SemanticRole::image;
        descriptor.name = "Waterfall tearing diagnostic";
        descriptor.value = paused() ? "paused" : "running";
        return descriptor;
    }

    // The surface follows the control's size in device pixels, so that the
    // window copies each frame instead of scaling it (UI thread; the
    // producer reconfigures the surface before its next frame).
    void arrange(Rect final_bounds) override {
        Control::arrange(final_bounds);
        request_device_size();
    }

protected:
    void on_attached_to_window() override {
        Control::on_attached_to_window();
        request_device_size();
        std::shared_ptr<WaterfallSurface> self;
        try {
            self = std::static_pointer_cast<WaterfallSurface>(shared_from_this());
        } catch (...) {
            return;
        }
        direct_live_registered_ = window() != nullptr &&
            (*window()).queue_live_surface_presentation(self, surface_);
        if (!direct_live_registered_) {
            connect_surface_wake();
            request_surface_paint();
        }
    }

    void on_detached_from_window() noexcept override {
        direct_live_registered_ = false;
        disconnect_surface_wake();
        Control::on_detached_from_window();
    }

    void on_dispose() noexcept override {
        disconnect_surface_wake();
        stop_worker();
        write_trace();
        Control::on_dispose();
    }

private:
    struct PaintRequest final {
        std::weak_ptr<WaterfallSurface> surface;

        void operator()() const {
            const std::shared_ptr<WaterfallSurface> target = surface.lock();
            if (!target || !(*target).is_alive() || !(*target).attached()) return;
            (*target).wake_posts_.fetch_add(1U, std::memory_order_relaxed);
            (*target).invalidate((*target).client_rectangle());
            if ((*target).full_window_repaint_.load(std::memory_order_acquire)) {
                Window* owner = (*target).window();
                if (owner) (*(*owner).root()).invalidate(invalidation::paint_only);
            }
            // paint_queued_ remains set until on_paint samples a generation.
            // The producer therefore cannot create a UI callback flood.
        }
    };

    struct SurfaceWakeRequest final {
        std::weak_ptr<WaterfallSurface> surface;

        void operator()() const {
            const std::shared_ptr<WaterfallSurface> target = surface.lock();
            if (target) (*target).request_surface_paint();
        }
    };

    void request_device_size() noexcept {
        const Window* const owner = window();
        if (owner == nullptr) return;
        const Rect bounds = client_rectangle();
        const double scale = (*owner).scale();
        const double width = std::clamp(std::round(bounds.width * scale), 1.0,
                                        static_cast<double>(maximum_surface_side));
        const double height = std::clamp(std::round(bounds.height * scale), 1.0,
                                         static_cast<double>(maximum_surface_side));
        SurfaceSize size;
        size.width = static_cast<std::uint32_t>(width);
        size.height = static_cast<std::uint32_t>(height);
        requested_size_.store(pack_surface_size(size), std::memory_order_release);
    }

    static void write_pixel(std::span<std::byte> pixels, std::uint32_t width,
                            std::uint32_t x, std::uint32_t y, std::uint8_t red,
                            std::uint8_t green, std::uint8_t blue) noexcept {
        const std::size_t offset =
            (static_cast<std::size_t>(y) * width + x) * 4U;
        pixels[offset] = static_cast<std::byte>(blue);
        pixels[offset + 1U] = static_cast<std::byte>(green);
        pixels[offset + 2U] = static_cast<std::byte>(red);
        pixels[offset + 3U] = std::byte{0xff};
    }

    static std::uint32_t next_random(std::uint32_t& state) noexcept {
        state ^= state << 13U;
        state ^= state >> 17U;
        state ^= state << 5U;
        return state;
    }

    static std::uint32_t peak(std::uint32_t x,
                              std::uint32_t center,
                              std::uint32_t width,
                              std::uint32_t strength) noexcept {
        const std::uint32_t distance = x > center ? x - center : center - x;
        return distance >= width ? 0U : strength * (width - distance) / width;
    }

    static void waterfall_color(std::uint32_t intensity,
                                std::uint8_t& red, std::uint8_t& green,
                                std::uint8_t& blue) noexcept {
        intensity = std::min<std::uint32_t>(intensity, 255U);
        if (intensity < 64U) {
            red = 0U;
            green = static_cast<std::uint8_t>(intensity / 3U);
            blue = static_cast<std::uint8_t>(24U + intensity * 3U);
        } else if (intensity < 128U) {
            red = 0U;
            green = static_cast<std::uint8_t>((intensity - 64U) * 3U);
            blue = 255U;
        } else if (intensity < 192U) {
            red = static_cast<std::uint8_t>((intensity - 128U) * 4U);
            green = 220U;
            blue = static_cast<std::uint8_t>(255U - (intensity - 128U) * 4U);
        } else {
            red = 255U;
            green = static_cast<std::uint8_t>(220U - (intensity - 192U) * 2U);
            blue = static_cast<std::uint8_t>((intensity - 192U) * 2U);
        }
    }

    // The design column of pixel column `x` in a frame `width` pixels wide.
    static std::uint32_t design_column(std::uint32_t x, std::uint32_t width) noexcept {
        const std::uint32_t column = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(x) * surface_width / width);
        return column;
    }

    // The waterfall so far, independent of the frame's size: one entry per
    // waterfall row produced (a ring of history_capacity_rows), each that
    // row's intensity at every design column, after contrast. A resize draws
    // the frame again from it, so every row is drawn from its own values at
    // the new size, never from pixels already scaled at earlier sizes (which
    // left the waterfall in bands, each scaled by a different amount).
    // Storage is reserved once; the oldest rows are overwritten. Owned by
    // the producer thread.
    struct WaterfallHistory final {
        std::vector<std::uint8_t> intensities;
        // The ring slot of the newest row, and how many rows are held.
        std::size_t newest{};
        std::size_t count{};
    };
    static constexpr std::size_t history_capacity_rows = 4096U;

    // The spectrum strip's height in pixel rows of a frame of `size`.
    static std::uint32_t spectrum_rows(SurfaceSize size) noexcept {
        const std::uint32_t strip = std::max<std::uint32_t>(
            1U, static_cast<std::uint32_t>(static_cast<std::uint64_t>(spectrum_height) *
                                           size.height / surface_height));
        return strip;
    }

    // The design-column intensities of the history row `age` rows older
    // than the newest (0 is the newest); `age` is below history.count.
    static const std::uint8_t* history_row(const WaterfallHistory& history, std::size_t age) noexcept {
        const std::size_t slot = (history.newest + history_capacity_rows - age) % history_capacity_rows;
        const std::uint8_t* const row = history.intensities.data() + slot * surface_width;
        return row;
    }

    // Draws pixel row `y` of a frame `width` pixels wide from one history
    // row's design-column intensities.
    static void draw_history_row(std::span<std::byte> staging, std::uint32_t width, std::uint32_t y,
                                 const std::uint8_t* intensities) noexcept {
        for (std::uint32_t x = 0U; x < width; ++x) {
            const std::uint32_t design_x = design_column(x, width);
            std::uint8_t red{};
            std::uint8_t green{};
            std::uint8_t blue{};
            waterfall_color(intensities[design_x], red, green, blue);
            write_pixel(staging, width, x, y, red, green, blue);
        }
    }

    // Draws the whole waterfall of a frame of `size` from `history`, the
    // newest row at the top; rows older than the history take the floor
    // colour (intensity 0). The spectrum strip is left to render_candidate.
    static void draw_waterfall_from_history(std::span<std::byte> staging, SurfaceSize size,
                                            const WaterfallHistory& history) noexcept {
        const std::uint32_t strip = spectrum_rows(size);
        const std::uint32_t waterfall_rows = size.height > strip ? size.height - strip : 0U;
        static constexpr std::array<std::uint8_t, surface_width> floor{};
        for (std::uint32_t y = 0U; y < waterfall_rows; ++y) {
            const std::uint8_t* const row = y < history.count ? history_row(history, y) : floor.data();
            draw_history_row(staging, size.width, strip + y, row);
        }
    }

    // Renders the next frame into `staging`, `size` pixels in rows of
    // size.width, scrolling the waterfall below the spectrum strip down by
    // the rows per frame; the new rows join `history`. Design coordinates
    // (surface_width by surface_height) map onto the frame's pixels.
    void render_candidate(std::span<std::byte> staging, SurfaceSize size,
                          std::uint64_t frame_index, WaterfallHistory& history) noexcept {
        const std::uint32_t width = size.width;
        const std::uint32_t strip = spectrum_rows(size);
        const std::uint32_t rows = rows_per_frame_.load(std::memory_order_acquire);
        const std::size_t row_bytes = static_cast<std::size_t>(width) * 4U;
        const std::size_t waterfall_rows = size.height > strip ? size.height - strip : 0U;
        const std::size_t shifted_rows = std::min<std::size_t>(rows, waterfall_rows);
        std::byte* const waterfall =
            staging.data() + static_cast<std::size_t>(strip) * row_bytes;
        if (waterfall_rows > shifted_rows) {
            std::memmove(waterfall + shifted_rows * row_bytes, waterfall,
                         (waterfall_rows - shifted_rows) * row_bytes);
        }

        const std::uint32_t contrast = contrast_.load(std::memory_order_acquire);
        std::uint32_t random =
            static_cast<std::uint32_t>(0x9e3779b9U ^ frame_index * 2654435761ULL);
        // The new rows, oldest first, so that row 0 (the top) is the newest
        // in the history: each is made at the design columns, kept, then drawn.
        for (std::size_t made = shifted_rows; made > 0U; --made) {
            const std::uint32_t y = static_cast<std::uint32_t>(made - 1U);
            history.newest = (history.newest + 1U) % history_capacity_rows;
            history.count = std::min(history.count + 1U, history_capacity_rows);
            std::uint8_t* const row = history.intensities.data() + history.newest * surface_width;
            for (std::uint32_t design_x = 0U; design_x < surface_width; ++design_x) {
                const std::uint32_t noise = next_random(random) & 63U;
                std::uint32_t intensity = 18U + noise;
                intensity += peak(design_x, 185U, 7U, 185U);
                intensity += peak(design_x, 428U, 18U, 112U);
                intensity += peak(design_x, 716U, 5U, 225U);
                intensity += peak(design_x, 842U, 44U, 95U);
                intensity = intensity * contrast / 100U;
                row[design_x] = static_cast<std::uint8_t>(std::min<std::uint32_t>(intensity, 255U));
            }
            draw_history_row(staging, width, strip + y, row);
        }

        // Rebuild the spectrum strip every candidate. The single bright sweep
        // line must remain vertically continuous; a split or stepped line is
        // a visible tear rather than ambiguous waterfall motion. Each design
        // grid line is one pixel wide at any size: the first pixel row or
        // column whose design coordinate is on the line.
        const double design_rows_per_row = static_cast<double>(spectrum_height) / strip;
        const double pixels_per_design_column = static_cast<double>(width) / surface_width;
        std::uint32_t previous_design_y = spectrum_height;
        for (std::uint32_t y = 0U; y < strip; ++y) {
            const std::uint32_t design_y = static_cast<std::uint32_t>(y * design_rows_per_row);
            const bool grid_row = design_y % 28U == 0U && design_y != previous_design_y;
            previous_design_y = design_y;
            std::uint32_t previous_design_x = surface_width;
            for (std::uint32_t x = 0U; x < width; ++x) {
                const std::uint32_t design_x = design_column(x, width);
                const bool grid_column = design_x % 128U == 0U && design_x != previous_design_x;
                previous_design_x = design_x;
                const bool grid = grid_column || grid_row;
                write_pixel(staging, width, x, y, grid ? 18U : 3U,
                            grid ? 31U : 8U, grid ? 46U : 14U);
            }
        }
        for (std::uint32_t x = 1U; x + 1U < width; ++x) {
            const double design_x = static_cast<double>(x) / pixels_per_design_column;
            const double phase = design_x * 0.031 +
                static_cast<double>(frame_index) * 0.027;
            const double lobe = 36.0 * std::exp(
                -std::pow((design_x - 716.0) / 72.0, 2.0));
            const double design_y = std::clamp(
                82.0 - 10.0 * std::sin(phase) - lobe, 5.0,
                static_cast<double>(spectrum_height - 5U));
            const std::uint32_t y = std::min<std::uint32_t>(
                static_cast<std::uint32_t>(design_y / design_rows_per_row), strip - 1U);
            write_pixel(staging, width, x, y, 218U, 240U, 255U);
            if (y + 1U < strip) {
                write_pixel(staging, width, x, y + 1U, 40U, 142U, 235U);
            }
        }
        const std::uint32_t design_sweep = static_cast<std::uint32_t>(
            (frame_index * 9U) % surface_width);
        const std::uint32_t sweep = std::min<std::uint32_t>(
            static_cast<std::uint32_t>(design_sweep * pixels_per_design_column), width - 1U);
        for (std::uint32_t y = 0U; y < strip; ++y) {
            write_pixel(staging, width, sweep, y, 255U, 241U, 76U);
        }

        // Optional work is producer-only. If this slows the controls, the
        // scheduler is stealing unrelated CPU rather than holding a UI lock.
        const std::uint32_t load = synthetic_load_.load(std::memory_order_acquire);
        volatile std::uint32_t sink = random;
        for (std::uint32_t index = 0U; index < load * 1800U; ++index) {
            sink = sink * 1664525U + 1013904223U;
        }
        static_cast<void>(sink);
    }

    void producer_loop() noexcept {
        SurfaceSize size;
        size.width = surface_width;
        size.height = surface_height;
        // Holds one frame of the current size; a resize draws it again at the
        // new size from the history. As elsewhere, a failed allocation ends
        // the process.
        std::vector<std::byte> staging(
            static_cast<std::size_t>(surface_width) * surface_height * 4U,
            std::byte{});
        WaterfallHistory history{};
        history.intensities.assign(history_capacity_rows * surface_width, std::uint8_t{});
        // Whether the surface has the control's size yet: until the
        // control's first arrangement there is nothing to render for.
        bool sized = false;
        std::chrono::steady_clock::time_point last_reconfigure{};
        std::uint64_t frame_index{};
        std::chrono::steady_clock::time_point next =
            std::chrono::steady_clock::now();
        const std::chrono::steady_clock::time_point trace_origin = next;
        trace_origin_nanoseconds_ = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(trace_origin.time_since_epoch()).count());
        const bool tracing = !trace_path_.empty();
        while (!stop_requested_.load(std::memory_order_acquire)) {
            if (paused_.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(4ms);
                next = std::chrono::steady_clock::now();
                continue;
            }
            // A producer whose window cannot present (covered, minimized, or
            // the surface hidden) idles: no rendering, publishing or UI wakes.
            if (!presentable_.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(16ms);
                next = std::chrono::steady_clock::now();
                continue;
            }
            // Follow the control's device size between frames, at most once
            // per reconfigure_interval: the surface starts a new epoch (the
            // window shows the previous epoch's frame, scaled, until this
            // one publishes, which it does at once), and the waterfall so
            // far is drawn again at the new size from its history.
            const std::chrono::steady_clock::time_point frame_started = std::chrono::steady_clock::now();
            const SurfaceSize requested =
                unpack_surface_size(requested_size_.load(std::memory_order_acquire));
            if (requested.width == 0U) {
                std::this_thread::sleep_for(4ms);
                next = std::chrono::steady_clock::now();
                continue;
            }
            const bool changed = requested.width != size.width || requested.height != size.height;
            const bool due = !sized || frame_started - last_reconfigure >= reconfigure_interval;
            if (changed && due && (*surface_).reconfigure(waterfall_description(requested))) {
                // As before, a failed allocation ends the process.
                staging.resize(static_cast<std::size_t>(requested.width) * requested.height * 4U);
                size = requested;
                draw_waterfall_from_history(staging, size, history);
                last_reconfigure = frame_started;
                sized = true;
            }
            if (!changed) sized = true;
            if (!sized) {
                std::this_thread::sleep_for(1ms);
                next = std::chrono::steady_clock::now();
                continue;
            }
            const std::chrono::steady_clock::time_point render_started = std::chrono::steady_clock::now();
            ++frame_index;
            producer_frames_.fetch_add(1U, std::memory_order_relaxed);
            render_candidate(staging, size, frame_index, history);
            const std::chrono::steady_clock::time_point publish_started = std::chrono::steady_clock::now();
            bool published = false;
            LiveSurfaceWriteLease lease = (*surface_).try_acquire_write();
            if (lease) {
                const std::span<std::byte> destination = lease.pixels();
                std::memcpy(destination.data(), staging.data(),
                            std::min(destination.size(), staging.size()));
                if (lease.publish()) {
                    published_frames_.fetch_add(1U, std::memory_order_relaxed);
                    published = true;
                }
            }
            if (tracing && trace_.size() < maximum_trace_entries) {
                const std::chrono::steady_clock::time_point finished = std::chrono::steady_clock::now();
                ProducerTraceEntry entry{};
                entry.started = nanoseconds_between(trace_origin, frame_started);
                entry.reconfigure_nanoseconds = nanoseconds_between(frame_started, render_started);
                entry.render_nanoseconds = nanoseconds_between(render_started, publish_started);
                entry.publish_nanoseconds = nanoseconds_between(publish_started, finished);
                entry.read_acquires = (*surface_).snapshot().read_acquires;
                entry.width = size.width;
                entry.height = size.height;
                entry.published = published;
                trace_.push_back(entry);
            }

            const std::uint32_t rate =
                producer_rate_.load(std::memory_order_acquire);
            const std::chrono::microseconds period = std::chrono::microseconds(
                std::max<std::uint32_t>(1U, 1'000'000U / rate));
            next += period;
            const std::chrono::steady_clock::time_point now =
                std::chrono::steady_clock::now();
            if (next > now) {
                std::this_thread::sleep_until(next);
            } else if (now - next > period * 4) {
                next = now;
            }
        }
    }

    void request_surface_paint() noexcept {
        if (paint_queued_.exchange(true, std::memory_order_acq_rel)) return;
        std::shared_ptr<WaterfallSurface> self;
        try {
            self = std::static_pointer_cast<WaterfallSurface>(shared_from_this());
        } catch (...) {
            paint_queued_.store(false, std::memory_order_release);
            return;
        }
        const std::weak_ptr<WaterfallSurface> weak = self;
        try {
            static_cast<void>(begin_invoke(PaintRequest{weak}));
        } catch (...) {
            paint_queued_.store(false, std::memory_order_release);
        }
    }

    void connect_surface_wake() {
        disconnect_surface_wake();
        if (!surface_ || window() == nullptr) return;
        const std::weak_ptr<WaterfallSurface> weak =
            std::static_pointer_cast<WaterfallSurface>(shared_from_this());
        surface_wake_ = (*surface_).connect_presentation_wake(
            SurfaceWakeRequest{weak});
    }

    void disconnect_surface_wake() noexcept {
        surface_wake_.disconnect();
        paint_queued_.store(false, std::memory_order_release);
    }

    void stop_worker() noexcept {
        if (worker_.joinable()) {
            stop_requested_.store(true, std::memory_order_release);
            worker_.join();
        }
    }

    // Writes the trace as CSV, once, after the producer has stopped (UI
    // thread). A file that cannot be opened leaves no trace.
    void write_trace() noexcept {
        if (trace_path_.empty() || worker_.joinable()) return;
        try {
            std::ofstream out(trace_path_, std::ios::trunc);
            out << "origin_ns " << trace_origin_nanoseconds_ << '\n';
            out << "started_ns,reconfigure_ns,render_ns,publish_ns,read_acquires,width,height,published\n";
            for (const ProducerTraceEntry& entry : trace_) {
                out << entry.started << ',' << entry.reconfigure_nanoseconds << ',' << entry.render_nanoseconds
                    << ',' << entry.publish_nanoseconds << ',' << entry.read_acquires << ',' << entry.width << ','
                    << entry.height << ',' << (entry.published ? 1 : 0) << '\n';
            }
        } catch (...) {
        }
        trace_path_.clear();
    }

    std::shared_ptr<LiveSurface> surface_;
    LiveSurfaceWakeConnection surface_wake_;
    // The optional trace: written by the producer only while it runs, read
    // by write_trace after it has stopped.
    std::string trace_path_;
    std::vector<ProducerTraceEntry> trace_;
    // The producer's start on the steady clock, for aligning the trace.
    std::uint64_t trace_origin_nanoseconds_{};
    std::thread worker_;
    std::atomic<bool> stop_requested_{};
    std::atomic<std::uint32_t> producer_rate_{240U};
    // The control's size in device pixels (pack_surface_size), 0 before its
    // first arrangement in a window.
    std::atomic<std::uint64_t> requested_size_{};
    std::atomic<std::uint32_t> rows_per_frame_{1U};
    std::atomic<std::uint32_t> contrast_{100U};
    std::atomic<std::uint32_t> synthetic_load_{0U};
    std::atomic<bool> paused_{};
    std::atomic<bool> presentable_{true};
    std::atomic<bool> full_window_repaint_{};
    bool direct_live_registered_{};
    std::atomic<bool> paint_queued_{};
    std::atomic<std::uint64_t> producer_frames_{};
    std::atomic<std::uint64_t> published_frames_{};
    std::atomic<std::uint64_t> paint_samples_{};
    std::atomic<std::uint64_t> wake_posts_{};
    std::atomic<std::uint64_t> last_painted_generation_{};
};

struct LabContext final {
    std::shared_ptr<WaterfallSurface> waterfall;
    std::shared_ptr<Label> producer_value;
    std::shared_ptr<Label> rows_value;
    std::shared_ptr<Label> contrast_value;
    std::shared_ptr<Label> load_value;
    std::shared_ptr<Label> metrics;
    std::shared_ptr<Label> mode;
    std::shared_ptr<Label> response;
    std::shared_ptr<Button> pause;
    std::vector<SubscriptionToken> subscriptions;
    std::unique_ptr<Timer> telemetry_timer;
    WaterfallSurface::Snapshot previous;
    std::chrono::steady_clock::time_point previous_time{
        std::chrono::steady_clock::now()};
    std::uint64_t response_count{};
};

void refresh_telemetry(const std::shared_ptr<LabContext>& context);

enum class LabValueKind { producer_rate, rows_per_frame, contrast, load };

struct LabValueChanged final {
    std::weak_ptr<LabContext> context;
    LabValueKind kind{};

    void operator()(double value) const {
        const std::shared_ptr<LabContext> state = context.lock();
        if (!state) return;
        const std::uint32_t rounded =
            static_cast<std::uint32_t>(std::lround(value));
        if (kind == LabValueKind::producer_rate) {
            (*(*state).waterfall).set_producer_rate(rounded);
            (*(*state).producer_value).set_text(std::to_string(rounded) + " Hz");
        } else if (kind == LabValueKind::rows_per_frame) {
            (*(*state).waterfall).set_rows_per_frame(rounded);
            (*(*state).rows_value).set_text(
                std::to_string(rounded) + (rounded == 1U ? " row" : " rows"));
        } else if (kind == LabValueKind::contrast) {
            (*(*state).waterfall).set_contrast(rounded);
            (*(*state).contrast_value).set_text(std::to_string(rounded) + "%");
        } else {
            (*(*state).waterfall).set_synthetic_load(rounded);
            (*(*state).load_value).set_text(std::to_string(rounded) + "%");
        }
    }
};

struct PauseClicked final {
    std::weak_ptr<LabContext> context;

    void operator()(ButtonBase&) const {
        const std::shared_ptr<LabContext> state = context.lock();
        if (!state) return;
        const bool next = !(*(*state).waterfall).paused();
        (*(*state).waterfall).set_paused(next);
        (*(*state).pause).text = next ? "Resume producer" : "Pause producer";
    }
};

struct ResponseClicked final {
    std::weak_ptr<LabContext> context;

    void operator()(ButtonBase&) const {
        const std::shared_ptr<LabContext> state = context.lock();
        if (!state) return;
        ++(*state).response_count;
        (*(*state).response).set_text(
            "tap count " + std::to_string((*state).response_count) +
            " · delivered on UI thread");
    }
};

struct FullRepaintChanged final {
    std::weak_ptr<LabContext> context;

    void operator()(CheckState checked) const {
        const std::shared_ptr<LabContext> state = context.lock();
        if (!state) return;
        const bool enabled = checked == CheckState::checked;
        (*(*state).waterfall).set_full_window_repaint(enabled);
        (*(*state).mode).set_text(enabled
            ? "BAD PATH (A/B CONTROL)\nEvery video sample dirties the complete retained window. Input contention here is expected evidence."
            : "GOOD PATH\nOnly the waterfall damage is sampled. Producer publication never waits for UI paint or input.");
    }
};

struct TelemetryTick final {
    std::weak_ptr<LabContext> context;

    void operator()() const {
        const std::shared_ptr<LabContext> state = context.lock();
        if (state) refresh_telemetry(state);
    }
};

std::shared_ptr<Label> label(std::string id, std::string text, Rect bounds,
                             TextStyleRole role = TextStyleRole::body) {
    std::shared_ptr<gui_forms::Label> result = make_control<Label>(StableId(std::move(id)), std::move(text));
    (*result).set_requested_bounds(bounds);
    (*result).set_text_style_role(role);
    (*result).set_vertical_alignment(VerticalAlignment::center);
    return result;
}

std::shared_ptr<TrackBar> slider(std::string id, double minimum,
                                 double maximum, double value, Rect bounds) {
    std::shared_ptr<gui_forms::TrackBar> result = make_control<TrackBar>(StableId(std::move(id)));
    (*result).set_requested_bounds(bounds);
    (*result).set_range(minimum, maximum);
    (*result).set_value(value);
    (*result).set_tick_frequency((maximum - minimum) / 8.0);
    (*result).set_show_ticks(true);
    return result;
}

void refresh_telemetry(const std::shared_ptr<LabContext>& context) {
    // The telemetry tick runs whether or not the window presents (UI timers
    // ignore occlusion), so it also tells the producer whether to run.
    WaterfallSurface& waterfall = *(*context).waterfall;
    const Window* const window = waterfall.attached_window();
    waterfall.set_presentable(window != nullptr && !(*window).occluded() &&
                              waterfall.effectively_visible());
    const std::chrono::steady_clock::time_point now =
        std::chrono::steady_clock::now();
    const double seconds = std::max(
        0.001, std::chrono::duration<double>(now - (*context).previous_time).count());
    const WaterfallSurface::Snapshot current = (*(*context).waterfall).snapshot();
    const double producer_fps =
        (current.producer_frames - (*context).previous.producer_frames) / seconds;
    const double published_fps =
        (current.published_frames - (*context).previous.published_frames) / seconds;
    const double sampled_fps =
        (current.surface.read_acquires -
         (*context).previous.surface.read_acquires) / seconds;
    const std::uint64_t lag = current.surface.published_generation >=
            current.surface.last_read_generation
        ? current.surface.published_generation -
            current.surface.last_read_generation
        : 0U;

    (*(*context).metrics).set_text(
        "producer   " + std::to_string(static_cast<unsigned>(producer_fps + 0.5)) +
        " fps\npublished  " +
        std::to_string(static_cast<unsigned>(published_fps + 0.5)) +
        " fps\nhost samples " +
        std::to_string(static_cast<unsigned>(sampled_fps + 0.5)) +
        " fps\nlatest lag " + std::to_string(lag) +
        " generations\ndropped writes " +
        std::to_string(current.surface.dropped_acquires) +
        "\nUI wake posts " + std::to_string(current.wake_posts) +
        (current.presentation_idle ? "\nproducer idle: not presented" : ""));
    (*context).previous = current;
    (*context).previous_time = now;
}

} // namespace

std::unique_ptr<Window> make_vsync_lab() {
    std::shared_ptr<LabContext> context = std::make_shared<LabContext>();
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("vsync.root"));
    (*root).set_background(Color::rgba(225, 231, 237));
    (*root).set_tag(context);
    // The root, header and footer are flat colour without a border, so a
    // size change repaints only what it exposed (Panel::paint_depends_on_size).

    std::shared_ptr<gui_forms::Panel> header = make_control<Panel>(StableId("vsync.header"));
    (*header).set_requested_bounds({0.0, 0.0, 0.0, 72.0});
    (*header).dock = DockStyle::top;
    (*header).set_background(Color::rgba(29, 47, 65));
    std::shared_ptr<Label> title = label("vsync.title", "GUI.Forms · live surface / input contention lab",
                       {22.0, 8.0, 760.0, 34.0}, TextStyleRole::title);
    (*title).set_foreground(Color::rgba(244, 249, 252));
    std::shared_ptr<Label> subtitle = label(
        "vsync.subtitle",
        "240 Hz producer · newest coherent frame · one outstanding UI paint · compositor-paced presentation",
        {24.0, 42.0, 920.0, 22.0}, TextStyleRole::caption);
    (*subtitle).set_foreground(Color::rgba(169, 205, 228));
    (*header).add_child(title);
    (*header).add_child(subtitle);

    std::shared_ptr<gui_forms::Panel> footer = make_control<Panel>(StableId("vsync.footer"));
    (*footer).set_requested_bounds({0.0, 0.0, 0.0, 42.0});
    (*footer).dock = DockStyle::bottom;
    (*footer).set_background(Color::rgba(209, 218, 226));
    std::shared_ptr<Label> footer_text = label(
        "vsync.footer.text",
        "Watch the yellow sweep for a split/step. Drag every slider while the waterfall runs; input must remain immediate.",
        {18.0, 5.0, 1080.0, 30.0});
    (*footer).add_child(footer_text);

    std::shared_ptr<gui_forms::Panel> controls = make_control<Panel>(StableId("vsync.controls"));
    (*controls).set_requested_bounds({0.0, 0.0, 300.0, 0.0});
    (*controls).dock = DockStyle::left;
    (*controls).padding = {12.0, 12.0, 12.0, 12.0};
    (*controls).set_border_style(BorderStyle::sunken);
    (*controls).add_child(label("vsync.controls.heading", "PRODUCER + INPUT",
                              {18.0, 12.0, 250.0, 28.0},
                              TextStyleRole::heading));

    std::shared_ptr<TrackBar> rate = slider("vsync.rate", 15.0, 1000.0, 240.0,
                       {18.0, 74.0, 252.0, 42.0});
    (*context).producer_value = label("vsync.rate.value", "240 Hz",
                                    {196.0, 45.0, 74.0, 24.0});
    (*controls).add_child(label("vsync.rate.label", "Producer rate",
                              {18.0, 45.0, 170.0, 24.0}));
    (*controls).add_child((*context).producer_value);
    (*controls).add_child(rate);

    std::shared_ptr<TrackBar> rows = slider("vsync.rows", 1.0, 12.0, 1.0,
                       {18.0, 152.0, 252.0, 42.0});
    (*context).rows_value = label("vsync.rows.value", "1 row",
                                {196.0, 123.0, 74.0, 24.0});
    (*controls).add_child(label("vsync.rows.label", "Rows / publish",
                              {18.0, 123.0, 170.0, 24.0}));
    (*controls).add_child((*context).rows_value);
    (*controls).add_child(rows);

    std::shared_ptr<TrackBar> contrast = slider("vsync.contrast", 25.0, 250.0, 100.0,
                           {18.0, 230.0, 252.0, 42.0});
    (*context).contrast_value = label("vsync.contrast.value", "100%",
                                    {196.0, 201.0, 74.0, 24.0});
    (*controls).add_child(label("vsync.contrast.label", "Color contrast",
                              {18.0, 201.0, 170.0, 24.0}));
    (*controls).add_child((*context).contrast_value);
    (*controls).add_child(contrast);

    std::shared_ptr<TrackBar> load = slider("vsync.load", 0.0, 100.0, 0.0,
                       {18.0, 308.0, 252.0, 42.0});
    (*context).load_value = label("vsync.load.value", "0%",
                                {196.0, 279.0, 74.0, 24.0});
    (*controls).add_child(label("vsync.load.label", "Producer CPU load",
                              {18.0, 279.0, 170.0, 24.0}));
    (*controls).add_child((*context).load_value);
    (*controls).add_child(load);

    (*context).pause = make_control<Button>(StableId("vsync.pause"), "Pause producer");
    (*(*context).pause).set_requested_bounds({18.0, 372.0, 122.0, 34.0});
    (*(*context).pause).set_visual_style(ButtonVisualStyle::accent);
    std::shared_ptr<gui_forms::Button> response_button = make_control<Button>(StableId("vsync.response.button"),
                                                "Responsiveness tap");
    (*response_button).set_requested_bounds({148.0, 372.0, 122.0, 34.0});
    (*context).response = label("vsync.response", "tap count 0",
                              {18.0, 410.0, 252.0, 26.0});
    (*controls).add_child((*context).pause);
    (*controls).add_child(response_button);
    (*controls).add_child((*context).response);

    std::shared_ptr<gui_forms::CheckBox> full_repaint = make_control<CheckBox>(StableId("vsync.full-repaint"),
                                               "Force full-window repaint (bad path)");
    (*full_repaint).set_requested_bounds({18.0, 454.0, 260.0, 32.0});
    (*full_repaint).set_indicator_style(ChoiceIndicatorStyle::modern);
    (*controls).add_child(full_repaint);
    std::shared_ptr<Label> warning = label(
        "vsync.warning",
        "The bad-path switch intentionally couples every video sample to the complete retained window. It is an A/B control, not the design.",
        {18.0, 490.0, 252.0, 78.0}, TextStyleRole::caption);
    (*warning).set_text_wrapping(TextWrapping::word);
    (*controls).add_child(warning);

    // Epistemic controls: these values are retained by the TrackBars but are
    // intentionally unobserved. Dragging them exercises input, retained state,
    // damage, and presentation without touching the producer or live surface.
    (*controls).add_child(label("vsync.noop-a.label", "No-op input A",
                              {18.0, 570.0, 118.0, 20.0},
                              TextStyleRole::caption));
    (*controls).add_child(label("vsync.noop-b.label", "No-op input B",
                              {152.0, 570.0, 118.0, 20.0},
                              TextStyleRole::caption));
    (*controls).add_child(slider("vsync.noop-a", 0.0, 100.0, 50.0,
                               {18.0, 592.0, 118.0, 34.0}));
    (*controls).add_child(slider("vsync.noop-b", 0.0, 100.0, 50.0,
                               {152.0, 592.0, 118.0, 34.0}));

    std::shared_ptr<gui_forms::Panel> metrics_panel = make_control<Panel>(StableId("vsync.metrics.panel"));
    (*metrics_panel).set_requested_bounds({0.0, 0.0, 286.0, 0.0});
    (*metrics_panel).dock = DockStyle::right;
    (*metrics_panel).padding = {12.0, 12.0, 12.0, 12.0};
    (*metrics_panel).set_border_style(BorderStyle::sunken);
    (*metrics_panel).add_child(label("vsync.metrics.heading", "LIVE TELEMETRY",
                                   {18.0, 12.0, 240.0, 28.0},
                                   TextStyleRole::heading));
    (*context).metrics = label("vsync.metrics", "warming up…",
                             {18.0, 52.0, 244.0, 184.0},
                             TextStyleRole::monospace);
    (*(*context).metrics).set_text_wrapping(TextWrapping::word);
    (*metrics_panel).add_child((*context).metrics);
    (*context).mode = label(
        "vsync.mode",
        "GOOD PATH\nOnly the waterfall damage is sampled. Producer publication never waits for UI paint or input.",
        {18.0, 252.0, 244.0, 110.0});
    (*(*context).mode).set_text_wrapping(TextWrapping::word);
    (*metrics_panel).add_child((*context).mode);
    std::shared_ptr<Label> expectations = label(
        "vsync.expectations",
        "Acceptance\n• sliders track continuously\n• tap increments immediately\n• waterfall never stops at fill\n• yellow sweep never tears\n• generation lag stays bounded",
        {18.0, 388.0, 244.0, 168.0}, TextStyleRole::caption);
    (*expectations).set_text_wrapping(TextWrapping::word);
    (*metrics_panel).add_child(expectations);

    std::shared_ptr<gui_forms::Panel> center = make_control<Panel>(StableId("vsync.center"));
    (*center).dock = DockStyle::fill;
    (*center).padding = {12.0, 12.0, 12.0, 12.0};
    (*center).set_background(Color::rgba(10, 17, 25));
    (*center).set_border_style(BorderStyle::sunken);
    (*context).waterfall =
        make_control<WaterfallSurface>(StableId("vsync.waterfall"));
    (*(*context).waterfall).dock = DockStyle::fill;
    (*center).add_child((*context).waterfall);

    // Docking consumes backmost-to-topmost. Author edge reservations first and
    // leave the fill surface last so it receives only the remaining client.
    (*root).add_child(header);
    (*root).add_child(footer);
    (*root).add_child(controls);
    (*root).add_child(metrics_panel);
    (*root).add_child(center);

    (*context).subscriptions.push_back((*rate).value_changed().subscribe(
        *root, LabValueChanged{context, LabValueKind::producer_rate}));
    (*context).subscriptions.push_back((*rows).value_changed().subscribe(
        *root, LabValueChanged{context, LabValueKind::rows_per_frame}));
    (*context).subscriptions.push_back((*contrast).value_changed().subscribe(
        *root, LabValueChanged{context, LabValueKind::contrast}));
    (*context).subscriptions.push_back((*load).value_changed().subscribe(
        *root, LabValueChanged{context, LabValueKind::load}));
    (*context).subscriptions.push_back((*(*context).pause).clicked().subscribe(
        *root, PauseClicked{context}));
    (*context).subscriptions.push_back((*response_button).clicked().subscribe(
        *root, ResponseClicked{context}));
    (*context).subscriptions.push_back((*full_repaint).check_state_changed().subscribe(
        *root, FullRepaintChanged{context}));

    std::unique_ptr<gui_forms::Window> window = std::make_unique<Window>(root, Size{1280.0, 780.0});
    (*context).telemetry_timer = std::make_unique<Timer>(*window, 250ms);
    (*context).subscriptions.push_back((*(*context).telemetry_timer).tick().subscribe(
        *root, TelemetryTick{context}));
    (*(*context).telemetry_timer).start();
    return window;
}

} // namespace gui_forms::vsync_lab
