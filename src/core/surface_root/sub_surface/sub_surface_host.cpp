#include "gui_forms/surface_root/sub_surface/sub_surface_host.hpp"

#include "gui_forms/window.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace gui_forms {

namespace {

// The device extent of a logical one at `scale`, rounded outward (resize
// rule 2); zero when it is empty or does not fit.
[[nodiscard]] std::uint32_t device_extent(double logical, double scale) noexcept {
    const double extent = std::ceil(logical * scale);
    if (!std::isfinite(extent) || extent < 1.0) return 0U;
    if (extent > static_cast<double>(std::numeric_limits<std::int32_t>::max())) return 0U;
    return static_cast<std::uint32_t>(extent);
}

} // namespace

SubSurfaceHost::SubSurfaceHost(StableId stable_id)
    : Control(std::move(stable_id)) {}

SubSurfaceHost::~SubSurfaceHost() {
    stop_thread();
}

void SubSurfaceHost::set_content(std::unique_ptr<SubSurfaceContent> content, bool opaque) {
    require_mutable();
    stop_thread();
    content_ = std::move(content);
    opaque_ = opaque;
    configure_region();
    invalidate(Dirty::paint);
}

SubSurfaceState SubSurfaceHost::state() const {
    if (!thread_) return SubSurfaceState::stopped;
    const SubSurfaceState state = (*thread_).state();
    return state;
}

SubSurfaceStatistics SubSurfaceHost::statistics() const {
    if (!thread_) return SubSurfaceStatistics{};
    const SubSurfaceStatistics statistics = (*thread_).statistics();
    return statistics;
}

WindowDispatcher SubSurfaceHost::dispatcher() const {
    if (!thread_) return WindowDispatcher{};
    const WindowDispatcher dispatcher = (*thread_).dispatcher();
    return dispatcher;
}

void SubSurfaceHost::arrange(Rect final_bounds) {
    Control::arrange(final_bounds);
    configure_region();
}

void SubSurfaceHost::on_paint(Painter& painter, Rect) {
    if (!surface_) return;
    const Rect bounds{0.0, 0.0, committed_arranged_bounds().width, committed_arranged_bounds().height};
    painter.draw_live_surface(surface_, bounds, 1.0);
}

void SubSurfaceHost::on_attached_to_window() {
    Control::on_attached_to_window();
    configure_region();
}

void SubSurfaceHost::on_detaching_from_window(SurfaceRoot& former_window) noexcept {
    // The window drops the LiveSurface registration with the control; the
    // thread is revoked and joined here, while the window still exists.
    stop_thread();
    Control::on_detaching_from_window(former_window);
}

void SubSurfaceHost::configure_region() {
    // A host inside a sub-surface has no window to compose into; it shows
    // nothing (nesting is not part of stage S2).
    Window* const window = attached_window();
    if (window == nullptr) return;
    if (!thread_ && !content_) return;
    const Rect bounds = committed_arranged_bounds();
    const double scale = (*window).scale();
    const std::uint32_t pixel_width = device_extent(bounds.width, scale);
    const std::uint32_t pixel_height = device_extent(bounds.height, scale);
    if (pixel_width == 0U || pixel_height == 0U) return;
    // Per axis (resize rules): only a changed device size or scale
    // reconfigures; a new arrangement at the same size signals nothing.
    const bool unchanged = thread_ && pixel_width == pixel_width_ && pixel_height == pixel_height_ &&
                           scale == configured_scale_;
    if (unchanged) return;
    const Size logical{bounds.width, bounds.height};
    if (!thread_) {
        LiveSurfaceDescription description;
        description.width = pixel_width;
        description.height = pixel_height;
        description.opaque = opaque_;
        std::shared_ptr<LiveSurface> surface = LiveSurface::create(description);
        if (!surface) return;
        std::unique_ptr<PaintFramebuffer> text_measure = (*window).create_framebuffer(Size{1.0, 1.0}, scale);
        thread_ = std::make_unique<SubSurfaceThread>(std::move(content_), surface, (*window).theme_ptr(),
                                                     std::move(text_measure));
        surface_ = std::move(surface);
    }
    ++content_epoch_;
    SubSurfaceConfiguration configuration;
    configuration.content_epoch = content_epoch_;
    configuration.logical_size = logical;
    configuration.scale = scale;
    configuration.pixel_width = pixel_width;
    configuration.pixel_height = pixel_height;
    configuration.opaque = opaque_;
    configuration.framebuffer = (*window).create_framebuffer(logical, scale);
    (*thread_).configure(std::move(configuration));
    pixel_width_ = pixel_width;
    pixel_height_ = pixel_height;
    configured_scale_ = scale;
    const Control::Ptr self = shared_from_this();
    static_cast<void>((*window).queue_live_surface_presentation(self, surface_));
    static_cast<void>((*window).require_live_surface_content(self, content_epoch_));
    invalidate(Dirty::paint);
}

void SubSurfaceHost::stop_thread() noexcept {
    if (!thread_) return;
    (*thread_).stop();
    thread_.reset();
    pixel_width_ = 0U;
    pixel_height_ = 0U;
    configured_scale_ = 0.0;
}

} // namespace gui_forms
