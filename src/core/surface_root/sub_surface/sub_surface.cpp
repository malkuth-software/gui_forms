#include "gui_forms/surface_root/sub_surface/sub_surface.hpp"

#include "sub_surface_channel.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace gui_forms {

SubSurface::SubSurface(Control::Ptr root, std::shared_ptr<ApplicationResources> resources)
    : SurfaceRoot(std::move(root), Size{}, std::move(resources)) {
    if (!resources_) {
        throw std::invalid_argument("GUI.Forms sub-surface requires application resources");
    }
    if (!(*resources_).check_access()) {
        throw std::logic_error("GUI.Forms sub-surface resources belong to another thread");
    }
    if (!root_) {
        throw std::invalid_argument("GUI.Forms sub-surface requires a retained root control");
    }
    if ((*root_).parent()) {
        throw std::logic_error("GUI.Forms sub-surface root may not already have a parent");
    }
    if (!(*root_).is_alive()) {
        throw std::logic_error("GUI.Forms sub-surface root may not be disposed");
    }
    // No damage yet: the root has no size until the first configuration
    // (apply_size damages the whole region then).
    attach_subtree(root_, {});
    // Last: a constructor that throws never leaves itself registered. The
    // resources are this sub-surface's own, so a theme set on them (a
    // configuration's) reaches this root alone.
    (*resources_).attach(*this);
}

SubSurface::~SubSurface() {
    release_tree();
    (*resources_).detach(*this);
}

void SubSurface::apply_size(Size logical_size) {
    require_root_thread("sub-surface size");
    const bool was_empty = client_size_.width <= 0.0 || client_size_.height <= 0.0;
    const bool changed = resize_surface(logical_size);
    if (!changed || !was_empty) return;
    // From no size, everything is exposed.
    const Rect whole{0.0, 0.0, client_size_.width, client_size_.height};
    add_damage_all_planes(whole);
    metrics_.record_dirty_mark(whole.area());
}

void SubSurface::apply_scale(double scale) {
    require_root_thread("sub-surface scale");
    static_cast<void>(set_surface_scale(scale));
}

namespace {

// A rectangle of device pixels, right and bottom exclusive.
struct PixelRectangle final {
    std::uint32_t left{};
    std::uint32_t top{};
    std::uint32_t right{};
    std::uint32_t bottom{};

    [[nodiscard]] bool empty() const noexcept { return right <= left || bottom <= top; }
};

[[nodiscard]] PixelRectangle unite_pixels(PixelRectangle first, PixelRectangle second) noexcept {
    if (first.empty()) return second;
    if (second.empty()) return first;
    PixelRectangle result;
    result.left = std::min(first.left, second.left);
    result.top = std::min(first.top, second.top);
    result.right = std::max(first.right, second.right);
    result.bottom = std::max(first.bottom, second.bottom);
    return result;
}

// The device pixels `logical` touches at `scale`, rounded outward and
// clipped to a width by height surface.
[[nodiscard]] PixelRectangle device_pixels_of(Rect logical, double scale, std::uint32_t width,
                                              std::uint32_t height) noexcept {
    PixelRectangle result;
    if (logical.empty() || !logical.finite()) return result;
    const double left = std::clamp(std::floor(logical.x * scale), 0.0, static_cast<double>(width));
    const double top = std::clamp(std::floor(logical.y * scale), 0.0, static_cast<double>(height));
    const double right =
        std::clamp(std::ceil((logical.x + logical.width) * scale), 0.0, static_cast<double>(width));
    const double bottom =
        std::clamp(std::ceil((logical.y + logical.height) * scale), 0.0, static_cast<double>(height));
    result.left = static_cast<std::uint32_t>(left);
    result.top = static_cast<std::uint32_t>(top);
    result.right = static_cast<std::uint32_t>(right);
    result.bottom = static_cast<std::uint32_t>(bottom);
    return result;
}

// Copies the rows of `area` from one BGRA8 buffer to another. Both buffers
// hold at least area.bottom rows of at least area.right pixels; they do not
// overlap (two distinct allocations).
void copy_pixel_rows(std::span<const std::byte> source, std::size_t source_row_bytes,
                     std::span<std::byte> destination, std::size_t destination_row_bytes,
                     PixelRectangle area) noexcept {
    const std::size_t row_offset = static_cast<std::size_t>(area.left) * 4U;
    const std::size_t row_length = static_cast<std::size_t>(area.right - area.left) * 4U;
    for (std::uint32_t row = area.top; row < area.bottom; ++row) {
        const std::byte* const from = source.data() + static_cast<std::size_t>(row) * source_row_bytes + row_offset;
        std::byte* const to =
            destination.data() + static_cast<std::size_t>(row) * destination_row_bytes + row_offset;
        std::memcpy(to, from, row_length);
    }
}

// How long a frame waits before trying again for a LiveSurface buffer when
// the presenter holds every one (rare: three buffers by default).
constexpr std::chrono::milliseconds publish_retry_interval{4};

// One sub-surface's thread: everything here is that thread's, from the
// moment run() starts until it returns and destroys the loop.
class SubSurfaceLoop final {
public:
    SubSurfaceLoop(std::shared_ptr<detail::SubSurfaceChannel> channel,
                   std::unique_ptr<SubSurfaceContent> content, std::shared_ptr<LiveSurface> surface,
                   std::shared_ptr<const Theme> theme, std::unique_ptr<PaintFramebuffer> text_measure) noexcept
        : channel_(std::move(channel)), content_(std::move(content)), surface_(std::move(surface)),
          theme_(std::move(theme)), text_measure_(std::move(text_measure)) {}

    // The thread's entry. Owns the loop and destroys it before returning, so
    // the content, its controls and both framebuffers die on this thread.
    static void run(std::unique_ptr<SubSurfaceLoop> loop) noexcept;

private:
    // Builds the tree and runs the loop until stopped. Exceptions leave the
    // thread as `failed` through run().
    void run_tree();
    // Waits for a configuration, a stop request, a wake or the root's next
    // frame request. Returns false on a stop request.
    [[nodiscard]] bool wait(const SubSurface& root, std::optional<SubSurfaceConfiguration>& configuration);
    void apply(SubSurface& root, SubSurfaceConfiguration& configuration);
    void produce(SubSurface& root);
    void publish();
    void record_end(SubSurfaceState state, std::string failure) noexcept;

    std::shared_ptr<detail::SubSurfaceChannel> channel_;
    std::unique_ptr<SubSurfaceContent> content_;
    std::shared_ptr<LiveSurface> surface_;
    std::shared_ptr<const Theme> theme_;
    std::unique_ptr<PaintFramebuffer> text_measure_;
    std::unique_ptr<PaintFramebuffer> framebuffer_{};
    std::uint64_t content_epoch_{};
    double scale_{1.0};
    // The LiveSurface's description, as this thread last set it.
    std::uint32_t surface_width_{};
    std::uint32_t surface_height_{};
    bool surface_opaque_{};
    bool configured_{};
    // The framebuffer pixels painted but not yet published.
    PixelRectangle unpublished_{};
    // The next publication copies the whole framebuffer (first frame of a
    // LiveSurface epoch).
    bool publish_whole_{};
    std::optional<FrameTime> retry_at_{};
};

void SubSurfaceLoop::run(std::unique_ptr<SubSurfaceLoop> loop) noexcept {
    SubSurfaceState end = SubSurfaceState::stopped;
    std::string failure;
    try {
        (*loop).run_tree();
    } catch (const std::exception& error) {
        end = SubSurfaceState::failed;
        failure = error.what();
    } catch (...) {
        end = SubSurfaceState::failed;
        failure = "GUI.Forms sub-surface content threw a non-standard exception";
    }
    // The content and the framebuffers go here, on this thread; the channel
    // stays alive through the copy below.
    const std::shared_ptr<detail::SubSurfaceChannel> channel = (*loop).channel_;
    loop.reset();
    const std::lock_guard<std::mutex> lock((*channel).mutex);
    (*channel).state = end;
    (*channel).failure = std::move(failure);
}

void SubSurfaceLoop::run_tree() {
    std::shared_ptr<ApplicationResources> resources = std::make_shared<ApplicationResources>();
    (*resources).set_theme(theme_);
    Control::Ptr built = (*content_).build_root();
    if (!built) {
        throw std::invalid_argument("GUI.Forms sub-surface content built no root control");
    }
    SubSurface root(std::move(built), std::move(resources));
    if (text_measure_) root.set_text_metrics_provider(&(*text_measure_).painter());
    root.set_dispatch_wake_handler(detail::SignalSubSurfaceChannel(channel_));
    root.set_paint_wake_handler(detail::SignalSubSurfaceChannel(channel_));
    {
        const std::lock_guard<std::mutex> lock((*channel_).mutex);
        (*channel_).dispatcher = root.dispatcher();
        (*channel_).state = SubSurfaceState::running;
    }
    std::optional<SubSurfaceConfiguration> configuration;
    while (wait(root, configuration)) {
        if (configuration) {
            apply(root, *configuration);
            configuration.reset();
        }
        const DispatchDrainResult drained = root.drain_posted_work();
        if (drained.remaining != 0U) {
            const std::lock_guard<std::mutex> lock((*channel_).mutex);
            (*channel_).signalled = true;
        }
        static_cast<void>(root.poll_frame_schedule(FrameClock::now()));
        root.flush();
        produce(root);
    }
    // Leaving the scope releases the tree on this thread (SubSurface's
    // destructor), before run() releases the content.
}

bool SubSurfaceLoop::wait(const SubSurface& root, std::optional<SubSurfaceConfiguration>& configuration) {
    std::optional<FrameTime> deadline = root.next_wake();
    if (retry_at_ && (!deadline || *retry_at_ < *deadline)) deadline = retry_at_;
    std::unique_lock<std::mutex> lock((*channel_).mutex);
    detail::SubSurfaceChannel& channel = *channel_;
    while (!channel.stop_requested && !channel.pending && !channel.signalled) {
        if (!deadline) {
            channel.wake.wait(lock);
            continue;
        }
        if (channel.wake.wait_until(lock, *deadline) == std::cv_status::timeout) break;
    }
    if (channel.stop_requested) return false;
    ++channel.statistics.loop_turns;
    configuration.swap(channel.pending);
    if (configuration) ++channel.statistics.configurations_applied;
    channel.signalled = false;
    return true;
}

void SubSurfaceLoop::apply(SubSurface& root, SubSurfaceConfiguration& configuration) {
    root.set_presentation_settings(configuration.presentation);
    const std::shared_ptr<ApplicationResources> resources = root.resources();
    if (configuration.theme && configuration.theme != (*resources).theme_ptr()) {
        (*resources).set_theme(configuration.theme);
    }
    if (configuration.scale != root.scale()) root.apply_scale(configuration.scale);
    root.apply_size(configuration.logical_size);
    content_epoch_ = configuration.content_epoch;
    if (configuration.framebuffer) {
        // The producer carries its content (resize rule 5): at an unchanged
        // scale the pixels both sizes cover stay, so only what the resize
        // exposed is painted again; at a new scale apply_scale marked the
        // whole tree.
        if (framebuffer_ && configuration.scale == scale_) {
            PixelRectangle overlap;
            overlap.right = std::min((*framebuffer_).width(), (*configuration.framebuffer).width());
            overlap.bottom = std::min((*framebuffer_).height(), (*configuration.framebuffer).height());
            copy_pixel_rows((*framebuffer_).pixels(), (*framebuffer_).row_bytes(),
                            (*configuration.framebuffer).pixels(), (*configuration.framebuffer).row_bytes(),
                            overlap);
        }
        framebuffer_ = std::move(configuration.framebuffer);
    }
    scale_ = configuration.scale;
    const bool resized = configuration.pixel_width != surface_width_ ||
                         configuration.pixel_height != surface_height_ ||
                         configuration.opaque != surface_opaque_;
    if (resized) {
        const LiveSurfaceSnapshot current = (*surface_).snapshot();
        const bool same_description = current.description.width == configuration.pixel_width &&
                                      current.description.height == configuration.pixel_height &&
                                      current.description.opaque == configuration.opaque;
        if (!same_description) {
            LiveSurfaceDescription description = current.description;
            description.width = configuration.pixel_width;
            description.height = configuration.pixel_height;
            description.opaque = configuration.opaque;
            // No write lease is held between frames, so this only fails for
            // a description LiveSurface refuses; the frames then stay at the
            // old size and the copy below is clipped to it.
            if ((*surface_).reconfigure(description)) {
                const std::lock_guard<std::mutex> lock((*channel_).mutex);
                ++(*channel_).statistics.surface_reconfigures;
            }
        }
        surface_width_ = configuration.pixel_width;
        surface_height_ = configuration.pixel_height;
        surface_opaque_ = configuration.opaque;
        publish_whole_ = true;
    }
    configured_ = true;
}

void SubSurfaceLoop::produce(SubSurface& root) {
    if (!configured_ || !framebuffer_) return;
    if (root.needs_frame()) {
        const DamageRegion taken = root.take_damage();
        if (!taken.empty() && (*framebuffer_).begin(root.image_resources(), taken.bounds())) {
            const std::optional<PaintReceipt> receipt = root.paint((*framebuffer_).painter(), taken.rectangles());
            (*framebuffer_).end();
            const PixelRectangle painted =
                device_pixels_of(taken.bounds(), scale_, (*framebuffer_).width(), (*framebuffer_).height());
            unpublished_ = unite_pixels(unpublished_, painted);
            // The framebuffer is this root's screen: the paint is presented
            // once it is there, whether or not a LiveSurface buffer is free.
            if (receipt) static_cast<void>(root.notify_presented(*receipt));
        }
    }
    if (unpublished_.empty() && !publish_whole_) return;
    publish();
}

void SubSurfaceLoop::publish() {
    LiveSurfaceWriteLease lease = (*surface_).try_acquire_write(true);
    if (!lease) {
        retry_at_ = FrameClock::now() + publish_retry_interval;
        const std::lock_guard<std::mutex> lock((*channel_).mutex);
        ++(*channel_).statistics.publish_retries;
        return;
    }
    retry_at_.reset();
    const std::uint32_t width = std::min(lease.width(), (*framebuffer_).width());
    const std::uint32_t height = std::min(lease.height(), (*framebuffer_).height());
    PixelRectangle area = unpublished_;
    if (publish_whole_) area = PixelRectangle{0U, 0U, width, height};
    area.right = std::min(area.right, width);
    area.bottom = std::min(area.bottom, height);
    area.left = std::min(area.left, area.right);
    area.top = std::min(area.top, area.bottom);
    copy_pixel_rows((*framebuffer_).pixels(), (*framebuffer_).row_bytes(), lease.pixels(),
                    static_cast<std::size_t>(lease.row_bytes()), area);
    const Rect damage{static_cast<double>(area.left), static_cast<double>(area.top),
                      static_cast<double>(area.right - area.left), static_cast<double>(area.bottom - area.top)};
    static_cast<void>(lease.publish(damage, content_epoch_));
    unpublished_ = PixelRectangle{};
    publish_whole_ = false;
    const std::lock_guard<std::mutex> lock((*channel_).mutex);
    ++(*channel_).statistics.frames_published;
    (*channel_).statistics.published_content_epoch = content_epoch_;
}

} // namespace

SubSurfaceThread::SubSurfaceThread(std::unique_ptr<SubSurfaceContent> content,
                                   std::shared_ptr<LiveSurface> surface,
                                   std::shared_ptr<const Theme> theme,
                                   std::unique_ptr<PaintFramebuffer> text_measure)
    : channel_(std::make_shared<detail::SubSurfaceChannel>()) {
    if (!content || !surface || !theme) {
        throw std::invalid_argument("GUI.Forms sub-surface requires content, a LiveSurface and a theme");
    }
    std::unique_ptr<SubSurfaceLoop> loop = std::make_unique<SubSurfaceLoop>(
        channel_, std::move(content), std::move(surface), std::move(theme), std::move(text_measure));
    thread_ = std::thread(&SubSurfaceLoop::run, std::move(loop));
}

SubSurfaceThread::~SubSurfaceThread() {
    stop();
}

void SubSurfaceThread::configure(SubSurfaceConfiguration configuration) {
    if (configuration.content_epoch <= last_content_epoch_) {
        throw std::invalid_argument("GUI.Forms sub-surface content epochs must grow");
    }
    if (!std::isfinite(configuration.scale) || configuration.scale <= 0.0 ||
        !std::isfinite(configuration.logical_size.width) || !std::isfinite(configuration.logical_size.height) ||
        configuration.logical_size.width <= 0.0 || configuration.logical_size.height <= 0.0 ||
        configuration.pixel_width == 0U || configuration.pixel_height == 0U) {
        throw std::invalid_argument("GUI.Forms sub-surface configuration needs a positive size and scale");
    }
    last_content_epoch_ = configuration.content_epoch;
    // A configuration replaced here is destroyed after the lock is released,
    // so its framebuffer never dies under the channel's lock.
    std::optional<SubSurfaceConfiguration> replaced;
    {
        const std::lock_guard<std::mutex> lock((*channel_).mutex);
        detail::SubSurfaceChannel& channel = *channel_;
        if (channel.stop_requested || channel.state == SubSurfaceState::failed) return;
        ++channel.statistics.configurations_received;
        if (channel.pending) {
            ++channel.statistics.configurations_coalesced;
            replaced.swap(channel.pending);
        }
        channel.pending.emplace(std::move(configuration));
        channel.wake.notify_one();
    }
}

void SubSurfaceThread::stop() noexcept {
    std::optional<SubSurfaceConfiguration> abandoned;
    {
        const std::lock_guard<std::mutex> lock((*channel_).mutex);
        (*channel_).stop_requested = true;
        abandoned.swap((*channel_).pending);
        (*channel_).wake.notify_one();
    }
    if (!thread_.joinable()) return;
    try {
        thread_.join();
    } catch (const std::system_error&) {
        // join refuses only a thread joining itself or a detached one, and
        // neither can happen: this object's thread never holds it.
    }
}

WindowDispatcher SubSurfaceThread::dispatcher() const {
    const std::lock_guard<std::mutex> lock((*channel_).mutex);
    return (*channel_).dispatcher;
}

SubSurfaceState SubSurfaceThread::state() const {
    const std::lock_guard<std::mutex> lock((*channel_).mutex);
    return (*channel_).state;
}

std::string SubSurfaceThread::failure() const {
    const std::lock_guard<std::mutex> lock((*channel_).mutex);
    return (*channel_).failure;
}

SubSurfaceStatistics SubSurfaceThread::statistics() const {
    const std::lock_guard<std::mutex> lock((*channel_).mutex);
    return (*channel_).statistics;
}

} // namespace gui_forms
