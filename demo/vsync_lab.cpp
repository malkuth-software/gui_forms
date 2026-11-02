#include "vsync_lab.hpp"

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/live_surface.hpp"
#include "gui_forms/range_controls.hpp"
#include "gui_forms/timer.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace gui_forms::vsync_lab {
namespace {

using namespace std::chrono_literals;

constexpr std::uint32_t surface_width = 1024U;
constexpr std::uint32_t surface_height = 480U;
constexpr std::uint32_t spectrum_height = 112U;

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
          surface_(LiveSurface::create({surface_width, surface_height})) {
        set_focusable(true);
        set_accessible_name("High-rate waterfall and tearing diagnostic");
        if (surface_) {
            worker_ = std::thread(&WaterfallSurface::producer_loop, this);
        }
    }

    ~WaterfallSurface() override {
        disconnect_surface_wake();
        stop_worker();
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

protected:
    void on_attached_to_window() override {
        Control::on_attached_to_window();
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

    static void write_pixel(std::span<std::byte> pixels, std::uint32_t x,
                            std::uint32_t y, std::uint8_t red,
                            std::uint8_t green, std::uint8_t blue) noexcept {
        const std::size_t offset =
            (static_cast<std::size_t>(y) * surface_width + x) * 4U;
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

    void render_candidate(std::span<std::byte> staging,
                          std::uint64_t frame_index) noexcept {
        const std::uint32_t rows = rows_per_frame_.load(std::memory_order_acquire);
        const std::size_t row_bytes = static_cast<std::size_t>(surface_width) * 4U;
        const std::size_t waterfall_rows = surface_height - spectrum_height;
        const std::size_t shifted_rows = std::min<std::size_t>(rows, waterfall_rows);
        std::byte* const waterfall =
            staging.data() + static_cast<std::size_t>(spectrum_height) * row_bytes;
        if (waterfall_rows > shifted_rows) {
            std::memmove(waterfall + shifted_rows * row_bytes, waterfall,
                         (waterfall_rows - shifted_rows) * row_bytes);
        }

        const std::uint32_t contrast = contrast_.load(std::memory_order_acquire);
        std::uint32_t random =
            static_cast<std::uint32_t>(0x9e3779b9U ^ frame_index * 2654435761ULL);
        for (std::uint32_t y = 0U; y < shifted_rows; ++y) {
            for (std::uint32_t x = 0U; x < surface_width; ++x) {
                const std::uint32_t noise = next_random(random) & 63U;
                std::uint32_t intensity = 18U + noise;
                intensity += peak(x, 185U, 7U, 185U);
                intensity += peak(x, 428U, 18U, 112U);
                intensity += peak(x, 716U, 5U, 225U);
                intensity += peak(x, 842U, 44U, 95U);
                intensity = intensity * contrast / 100U;
                std::uint8_t red{};
                std::uint8_t green{};
                std::uint8_t blue{};
                waterfall_color(intensity, red, green, blue);
                write_pixel(staging, x, spectrum_height + y, red, green, blue);
            }
        }

        // Rebuild the spectrum strip every candidate. The single bright sweep
        // line must remain vertically continuous; a split or stepped line is
        // a visible tear rather than ambiguous waterfall motion.
        for (std::uint32_t y = 0U; y < spectrum_height; ++y) {
            for (std::uint32_t x = 0U; x < surface_width; ++x) {
                const bool grid = (x % 128U == 0U) || (y % 28U == 0U);
                write_pixel(staging, x, y, grid ? 18U : 3U,
                            grid ? 31U : 8U, grid ? 46U : 14U);
            }
        }
        for (std::uint32_t x = 1U; x + 1U < surface_width; ++x) {
            const double phase = static_cast<double>(x) * 0.031 +
                static_cast<double>(frame_index) * 0.027;
            const double lobe = 36.0 * std::exp(
                -std::pow((static_cast<double>(x) - 716.0) / 72.0, 2.0));
            const std::uint32_t y = static_cast<std::uint32_t>(std::clamp(
                82.0 - 10.0 * std::sin(phase) - lobe, 5.0,
                static_cast<double>(spectrum_height - 5U)));
            write_pixel(staging, x, y, 218U, 240U, 255U);
            if (y + 1U < spectrum_height) {
                write_pixel(staging, x, y + 1U, 40U, 142U, 235U);
            }
        }
        const std::uint32_t sweep = static_cast<std::uint32_t>(
            (frame_index * 9U) % surface_width);
        for (std::uint32_t y = 0U; y < spectrum_height; ++y) {
            write_pixel(staging, sweep, y, 255U, 241U, 76U);
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
        std::vector<std::byte> staging(
            static_cast<std::size_t>(surface_width) * surface_height * 4U,
            std::byte{});
        std::uint64_t frame_index{};
        std::chrono::steady_clock::time_point next =
            std::chrono::steady_clock::now();
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
            ++frame_index;
            producer_frames_.fetch_add(1U, std::memory_order_relaxed);
            render_candidate(staging, frame_index);
            LiveSurfaceWriteLease lease = (*surface_).try_acquire_write();
            if (lease) {
                const std::span<std::byte> destination = lease.pixels();
                std::memcpy(destination.data(), staging.data(),
                            std::min(destination.size(), staging.size()));
                if (lease.publish()) {
                    published_frames_.fetch_add(1U, std::memory_order_relaxed);
                }
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

    std::shared_ptr<LiveSurface> surface_;
    LiveSurfaceWakeConnection surface_wake_;
    std::thread worker_;
    std::atomic<bool> stop_requested_{};
    std::atomic<std::uint32_t> producer_rate_{240U};
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
        (*(*state).pause).set_text(next ? "Resume producer" : "Pause producer");
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

    std::shared_ptr<gui_forms::Panel> header = make_control<Panel>(StableId("vsync.header"));
    (*header).set_requested_bounds({0.0, 0.0, 0.0, 72.0});
    (*header).set_dock(DockStyle::top);
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
    (*footer).set_dock(DockStyle::bottom);
    (*footer).set_background(Color::rgba(209, 218, 226));
    std::shared_ptr<Label> footer_text = label(
        "vsync.footer.text",
        "Watch the yellow sweep for a split/step. Drag every slider while the waterfall runs; input must remain immediate.",
        {18.0, 5.0, 1080.0, 30.0});
    (*footer).add_child(footer_text);

    std::shared_ptr<gui_forms::Panel> controls = make_control<Panel>(StableId("vsync.controls"));
    (*controls).set_requested_bounds({0.0, 0.0, 300.0, 0.0});
    (*controls).set_dock(DockStyle::left);
    (*controls).set_padding({12.0, 12.0, 12.0, 12.0});
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
    (*metrics_panel).set_dock(DockStyle::right);
    (*metrics_panel).set_padding({12.0, 12.0, 12.0, 12.0});
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
    (*center).set_dock(DockStyle::fill);
    (*center).set_padding({12.0, 12.0, 12.0, 12.0});
    (*center).set_background(Color::rgba(10, 17, 25));
    (*center).set_border_style(BorderStyle::sunken);
    (*context).waterfall =
        make_control<WaterfallSurface>(StableId("vsync.waterfall"));
    (*(*context).waterfall).set_dock(DockStyle::fill);
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
