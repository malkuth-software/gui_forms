// Stage S2.2: a SubSurface built, laid out and painted on its own thread,
// publishing into a LiveSurface its SubSurfaceHost composes (threaded
// sub-surfaces design note sections 2, 6, 7 and 9).

#include "gui_forms/gui_forms.hpp"
#include "gui_forms/surface_root/sub_surface/sub_surface_host.hpp"
#include "gui_forms/paint_framebuffer.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using namespace gui_forms;

// Throws on failure, so the check runs in every build type (Release defines
// NDEBUG); an uncaught throw fails the test.
void require(const bool condition, const char* const message) {
    if (!condition) throw std::runtime_error(message);
}

// A CPU stand-in for a host raster that really writes pixels: fill_rect
// stores the colour (opaque test colours only, no blending) into the
// rectangle under the current translation and clips, at the framebuffer's
// scale. Everything else draws nothing.
class FillPainter final : public Painter {
public:
    FillPainter(std::vector<std::byte>& pixels, std::uint32_t width, std::uint32_t height, double scale)
        : pixels_(pixels), width_(width), height_(height), scale_(scale) {
        states_.push_back(State{Point{}, Rect{0.0, 0.0, static_cast<double>(width) / scale,
                                              static_cast<double>(height) / scale}});
    }
    void save() override { states_.push_back(states_.back()); }
    void restore() override {
        if (states_.size() > 1U) states_.pop_back();
    }
    void translate(Point offset) override {
        states_.back().offset.x += offset.x;
        states_.back().offset.y += offset.y;
    }
    void clip_rect(Rect rect) override {
        const Rect placed{rect.x + states_.back().offset.x, rect.y + states_.back().offset.y, rect.width,
                          rect.height};
        states_.back().clip = Rect::intersection(states_.back().clip, placed);
    }
    void fill_rect(Rect rect, Color color) override {
        const Rect placed{rect.x + states_.back().offset.x, rect.y + states_.back().offset.y, rect.width,
                          rect.height};
        const Rect visible = Rect::intersection(placed, states_.back().clip);
        if (visible.empty()) return;
        const std::uint32_t left = pixel(visible.x, width_);
        const std::uint32_t top = pixel(visible.y, height_);
        const std::uint32_t right = pixel(visible.x + visible.width, width_);
        const std::uint32_t bottom = pixel(visible.y + visible.height, height_);
        for (std::uint32_t row = top; row < bottom; ++row) {
            for (std::uint32_t column = left; column < right; ++column) {
                const std::size_t at = (static_cast<std::size_t>(row) * width_ + column) * 4U;
                pixels_[at] = static_cast<std::byte>(color.blue);
                pixels_[at + 1U] = static_cast<std::byte>(color.green);
                pixels_[at + 2U] = static_cast<std::byte>(color.red);
                pixels_[at + 3U] = static_cast<std::byte>(color.alpha);
            }
        }
    }
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}

private:
    struct State final {
        Point offset{};
        Rect clip{};
    };
    [[nodiscard]] std::uint32_t pixel(double logical, std::uint32_t limit) const noexcept {
        const double device = std::clamp(std::round(logical * scale_), 0.0, static_cast<double>(limit));
        return static_cast<std::uint32_t>(device);
    }

    std::vector<std::byte>& pixels_;
    std::uint32_t width_{};
    std::uint32_t height_{};
    double scale_{1.0};
    std::vector<State> states_;
};

// Owns its pixels and its painter, so it may move to another thread with
// nothing left behind on the one that made it.
class FillFramebuffer final : public PaintFramebuffer {
public:
    FillFramebuffer(std::uint32_t width, std::uint32_t height, double scale)
        : width_(width), height_(height), pixels_(static_cast<std::size_t>(width) * height * 4U),
          painter_(pixels_, width, height, scale) {}
    bool begin(const ImageRegistry&, Rect) override { return true; }
    void end() override {}
    Painter& painter() noexcept override { return painter_; }
    std::span<std::byte> pixels() noexcept override { return pixels_; }
    std::size_t row_bytes() const noexcept override { return static_cast<std::size_t>(width_) * 4U; }
    std::uint32_t width() const noexcept override { return width_; }
    std::uint32_t height() const noexcept override { return height_; }

private:
    std::uint32_t width_{};
    std::uint32_t height_{};
    std::vector<std::byte> pixels_;
    FillPainter painter_;
};

// The window's framebuffer painter: makes FillFramebuffers. It keeps no
// state, so the window's thread may call it while sub-surfaces paint.
class FillRaster final : public Painter {
public:
    std::unique_ptr<PaintFramebuffer> create_framebuffer(Size logical_size, double scale) override {
        const std::uint32_t width = static_cast<std::uint32_t>(std::ceil(logical_size.width * scale));
        const std::uint32_t height = static_cast<std::uint32_t>(std::ceil(logical_size.height * scale));
        if (width == 0U || height == 0U) return {};
        std::unique_ptr<PaintFramebuffer> result = std::make_unique<FillFramebuffer>(width, height, scale);
        return result;
    }
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
};

// What the sub-surface's thread reports back to the test: written there,
// read here, under the mutex. The control is held weakly, so the test never
// keeps a sub-surface control alive past its thread.
struct Probe final {
    std::mutex mutex;
    std::weak_ptr<DrawingSurface> surface;
    std::thread::id built_on{};
    bool saw_no_window{};
    bool saw_surface_root{};
    double text_scale{};
    const Theme* application_theme{};
};

class SolidContent final : public SubSurfaceContent {
public:
    SolidContent(std::shared_ptr<Probe> probe, Color color) : probe_(std::move(probe)), color_(color) {}
    Control::Ptr build_root() override {
        std::shared_ptr<DrawingSurface> surface = std::make_shared<DrawingSurface>(StableId("sub.solid"));
        (*surface).set_background(color_);
        const std::lock_guard<std::mutex> lock((*probe_).mutex);
        (*probe_).surface = surface;
        (*probe_).built_on = std::this_thread::get_id();
        return surface;
    }

private:
    std::shared_ptr<Probe> probe_;
    Color color_{};
};

class NullContent final : public SubSurfaceContent {
public:
    Control::Ptr build_root() override { return {}; }
};

// Posted to the sub-surface's thread: repaints its root in a new colour and
// records what a control there sees of its roots.
class Recolour final {
public:
    Recolour(std::shared_ptr<Probe> probe, Color color) : probe_(std::move(probe)), color_(color) {}
    void operator()() const {
        const std::lock_guard<std::mutex> lock((*probe_).mutex);
        const std::shared_ptr<DrawingSurface> surface = (*probe_).surface.lock();
        if (!surface) return;
        (*surface).set_background(color_);
        (*probe_).saw_no_window = (*surface).attached_window() == nullptr;
        (*probe_).saw_surface_root = (*surface).attached_surface_root() != nullptr;
        if (SurfaceRoot* const root = (*surface).attached_surface_root()) {
            (*probe_).text_scale = (*root).presentation_settings().text_scale;
            (*probe_).application_theme = &(*root).application_theme();
        }
    }

private:
    std::shared_ptr<Probe> probe_;
    Color color_{};
};

constexpr std::chrono::seconds wait_limit{10};

// Waits until the surface's newest frame carries at least `tag`.
[[nodiscard]] bool wait_for_content_tag(const LiveSurface& surface, std::uint64_t tag) {
    const std::chrono::steady_clock::time_point limit = std::chrono::steady_clock::now() + wait_limit;
    while (std::chrono::steady_clock::now() < limit) {
        const LiveSurfaceSnapshot snapshot = surface.snapshot();
        if (snapshot.has_frame && !snapshot.carries_previous_frame && snapshot.content_tag >= tag) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

[[nodiscard]] bool wait_for_state(const SubSurfaceThread& thread, SubSurfaceState state) {
    const std::chrono::steady_clock::time_point limit = std::chrono::steady_clock::now() + wait_limit;
    while (std::chrono::steady_clock::now() < limit) {
        if (thread.state() == state) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

[[nodiscard]] bool wait_for_published(const SubSurfaceThread& thread, std::uint64_t frames) {
    const std::chrono::steady_clock::time_point limit = std::chrono::steady_clock::now() + wait_limit;
    while (std::chrono::steady_clock::now() < limit) {
        if (thread.statistics().frames_published >= frames) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

// Whether every pixel of the frame is `color`.
[[nodiscard]] bool frame_is(const LiveSurfaceFrame& frame, Color color) {
    if (!frame) return false;
    const std::span<const std::byte> pixels = frame.pixels();
    for (std::uint32_t row = 0; row < frame.height(); ++row) {
        for (std::uint32_t column = 0; column < frame.width(); ++column) {
            const std::size_t at = static_cast<std::size_t>(row) * frame.row_bytes() + column * 4U;
            if (pixels[at] != static_cast<std::byte>(color.blue) ||
                pixels[at + 1U] != static_cast<std::byte>(color.green) ||
                pixels[at + 2U] != static_cast<std::byte>(color.red) ||
                pixels[at + 3U] != static_cast<std::byte>(color.alpha)) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] SubSurfaceConfiguration configuration_of(std::uint64_t epoch, std::uint32_t width,
                                                       std::uint32_t height) {
    SubSurfaceConfiguration configuration;
    configuration.content_epoch = epoch;
    configuration.logical_size = Size{static_cast<double>(width), static_cast<double>(height)};
    configuration.scale = 1.0;
    configuration.pixel_width = width;
    configuration.pixel_height = height;
    configuration.opaque = true;
    configuration.framebuffer = std::make_unique<FillFramebuffer>(width, height, 1.0);
    return configuration;
}

constexpr Color red = Color::rgba(200, 20, 30);
constexpr Color green = Color::rgba(10, 180, 40);

// The thread alone: it builds on its own thread, produces nothing before the
// first configuration, publishes tagged frames, applies posted work, carries
// newest-wins configurations, and stops by joining.
void thread_produces_tagged_frames() {
    std::shared_ptr<Probe> probe = std::make_shared<Probe>();
    std::shared_ptr<LiveSurface> surface = LiveSurface::create({40U, 20U});
    require(static_cast<bool>(surface), "surface");
    SubSurfaceThread thread(std::make_unique<SolidContent>(probe, red), surface, default_theme(), nullptr);
    require(wait_for_state(thread, SubSurfaceState::running), "thread reaches running");
    {
        const std::lock_guard<std::mutex> lock((*probe).mutex);
        require((*probe).built_on != std::thread::id{} && (*probe).built_on != std::this_thread::get_id(),
                "content built on the sub-surface's own thread");
    }
    require(!(*surface).snapshot().has_frame, "nothing produced before the first agreed size");

    thread.configure(configuration_of(1U, 40U, 20U));
    require(wait_for_content_tag(*surface, 1U), "first frame published at epoch 1");
    require(frame_is((*surface).acquire_latest(), red), "first frame is the content's colour");

    const DispatchOperation posted = thread.dispatcher().begin_invoke(Recolour(probe, green));
    static_cast<void>(posted);
    const std::chrono::steady_clock::time_point limit = std::chrono::steady_clock::now() + wait_limit;
    while (!frame_is((*surface).acquire_latest(), green) && std::chrono::steady_clock::now() < limit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(frame_is((*surface).acquire_latest(), green), "posted work repaints on the sub-surface");
    {
        const std::lock_guard<std::mutex> lock((*probe).mutex);
        require((*probe).saw_no_window, "a control in a sub-surface sees no window");
        require((*probe).saw_surface_root, "a control in a sub-surface sees its surface root");
    }

    // Idle: with nothing posted, dirty or due, the loop does not turn.
    const std::uint64_t turns = thread.statistics().loop_turns;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    require(thread.statistics().loop_turns == turns, "an idle sub-surface does not turn its loop");

    // Newest wins: a burst of configurations ends at the last one's size and
    // epoch; none is lost from the count.
    for (std::uint64_t epoch = 2U; epoch <= 21U; ++epoch) {
        const std::uint32_t width = 30U + static_cast<std::uint32_t>(epoch);
        thread.configure(configuration_of(epoch, width, 25U));
    }
    require(wait_for_content_tag(*surface, 21U), "the burst ends at epoch 21");
    const LiveSurfaceFrame last = (*surface).acquire_latest();
    require(last.width() == 51U && last.height() == 25U, "the newest configuration's device size");
    require(frame_is(last, green), "the resized frame is whole");
    const SubSurfaceStatistics statistics = thread.statistics();
    require(statistics.configurations_received == 21U, "every configuration counted");
    require(statistics.configurations_applied + statistics.configurations_coalesced == 21U,
            "each configuration applied or coalesced");
    require(statistics.published_content_epoch == 21U, "published epoch is the newest");

    bool refused = false;
    try {
        thread.configure(configuration_of(21U, 10U, 10U));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "a content epoch that does not grow is refused");

    thread.stop();
    require(thread.state() == SubSurfaceState::stopped, "stopped after stop");
    const std::uint64_t published = thread.statistics().frames_published;
    thread.configure(configuration_of(30U, 12U, 12U));
    require(thread.statistics().frames_published == published, "a configuration after stop is dropped");
    {
        const std::lock_guard<std::mutex> lock((*probe).mutex);
        require((*probe).surface.expired(), "the tree is released when the thread stops");
    }
    require(!thread.dispatcher().window_alive(), "the dispatcher is closed after stop");
}

void content_failure_is_reported() {
    std::shared_ptr<LiveSurface> surface = LiveSurface::create({8U, 8U});
    SubSurfaceThread thread(std::make_unique<NullContent>(), surface, default_theme(), nullptr);
    require(wait_for_state(thread, SubSurfaceState::failed), "a null root fails the sub-surface");
    require(!thread.failure().empty(), "the failure is named");
    thread.configure(configuration_of(1U, 8U, 8U));
    thread.stop();
    require(thread.state() == SubSurfaceState::failed, "stop keeps the failure");
}

// Start and stop in every phase: before the tree exists, before the first
// configuration, and while frames are produced.
void start_stop_stress() {
    for (int round = 0; round < 60; ++round) {
        std::shared_ptr<Probe> probe = std::make_shared<Probe>();
        std::shared_ptr<LiveSurface> surface = LiveSurface::create({16U, 16U});
        SubSurfaceThread thread(std::make_unique<SolidContent>(probe, red), surface, default_theme(), nullptr);
        if (round % 3 >= 1) thread.configure(configuration_of(1U, 16U, 16U));
        if (round % 3 == 2) {
            for (std::uint64_t epoch = 2U; epoch < 6U; ++epoch) {
                thread.configure(configuration_of(epoch, 16U + static_cast<std::uint32_t>(epoch), 16U));
                static_cast<void>(thread.dispatcher().window_alive());
            }
        }
        thread.stop();
        const std::lock_guard<std::mutex> lock((*probe).mutex);
        require((*probe).surface.expired(), "every round releases its tree");
    }
}

// The host control in a window: starts on attach at its first size,
// registers its surface for presentation with the pairing requirement,
// reconfigures on a device-size change only, and stops on detach.
void host_in_window() {
    std::shared_ptr<Panel> root = std::make_shared<Panel>(StableId("sub.root"));
    std::shared_ptr<SubSurfaceHost> host = std::make_shared<SubSurfaceHost>(StableId("sub.host"));
    (*host).set_requested_bounds(Rect{10.0, 10.0, 60.0, 30.0});
    std::shared_ptr<Probe> probe = std::make_shared<Probe>();
    (*host).set_content(std::make_unique<SolidContent>(probe, red), true);
    require(!(*host).started(), "no thread before the host is in a window");
    (*root).add_child(host);
    FillRaster raster;
    Window window(root, Size{200.0, 100.0});
    window.set_framebuffer_painter(&raster);
    window.flush();
    require((*host).started(), "the host starts once arranged in a window");
    require((*host).content_epoch() == 1U, "one configuration for the first size");
    const std::shared_ptr<LiveSurface> surface = (*host).live_surface();
    require(static_cast<bool>(surface), "the host made its surface");
    require(wait_for_content_tag(*surface, 1U), "the sub-surface publishes at epoch 1");
    const LiveSurfaceFrame first = (*surface).acquire_latest();
    require(first.width() == 60U && first.height() == 30U && first.opaque(), "device-sized opaque frames");
    require(frame_is(first, red), "the host's region shows the content");
    const std::vector<LiveSurfacePresentation> presentations = window.take_live_surface_presentations(true);
    bool registered = false;
    for (const LiveSurfacePresentation& presentation : presentations) {
        if (presentation.surface == surface) registered = true;
    }
    require(registered, "the window composes the host's surface");

    // Same device size: nothing. New size: one new epoch, new surface size.
    (*host).invalidate(Dirty::arrange);
    window.flush();
    require((*host).content_epoch() == 1U, "an arrangement at the same size signals nothing");
    (*host).set_requested_bounds(Rect{10.0, 10.0, 80.0, 40.0});
    window.flush();
    require((*host).content_epoch() == 2U, "a new device size opens a new content epoch");
    require(wait_for_content_tag(*surface, 2U), "the sub-surface publishes at epoch 2");
    const LiveSurfaceFrame resized = (*surface).acquire_latest();
    require(resized.width() == 80U && resized.height() == 40U, "the surface follows the device size");
    require(frame_is(resized, red), "the resized frame is whole");

    // A scale change is a device-size change too.
    window.set_scale(2.0);
    window.flush();
    require((*host).content_epoch() == 3U, "a scale change opens a new content epoch");
    require(wait_for_content_tag(*surface, 3U), "the sub-surface publishes at epoch 3");
    const LiveSurfaceFrame scaled = (*surface).acquire_latest();
    require(scaled.width() == 160U && scaled.height() == 80U, "device pixels at scale 2");
    require(frame_is(scaled, red), "the scaled frame is whole");

    // The environment travels with a new epoch, without a new size: the
    // sub-surface takes the window's presentation settings and theme.
    PresentationSettings settings = window.presentation_settings();
    settings.text_scale = 1.5;
    window.set_presentation_settings(settings);
    window.flush();
    require((*host).content_epoch() == 4U, "presentation settings open a new content epoch");
    const std::shared_ptr<const Theme> other = Theme::create(windows_professional_theme_definition());
    window.set_theme(other);
    window.flush();
    require((*host).content_epoch() == 5U, "a theme change opens a new content epoch");
    require(wait_for_content_tag(*surface, 5U), "the sub-surface publishes at epoch 5");
    const DispatchOperation probed = (*host).dispatcher().begin_invoke(Recolour(probe, red));
    static_cast<void>(probed);
    const std::chrono::steady_clock::time_point limit = std::chrono::steady_clock::now() + wait_limit;
    bool environment_seen = false;
    while (!environment_seen && std::chrono::steady_clock::now() < limit) {
        {
            const std::lock_guard<std::mutex> lock((*probe).mutex);
            environment_seen = (*probe).text_scale == 1.5 && (*probe).application_theme == other.get();
        }
        if (!environment_seen) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(environment_seen, "the sub-surface shows the window's text scale and theme");

    static_cast<void>((*root).remove_child((*host).runtime_id()));
    require(!(*host).started(), "leaving the window stops and joins the thread");
    const std::lock_guard<std::mutex> lock((*probe).mutex);
    require((*probe).surface.expired(), "the sub-surface's tree is released");
}

} // namespace

int main() {
    thread_produces_tagged_frames();
    content_failure_is_reported();
    start_stop_stress();
    host_in_window();
    return 0;
}
