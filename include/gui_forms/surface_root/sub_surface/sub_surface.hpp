#pragma once

#include "gui_forms/surface_root/surface_root.hpp"

#include "gui_forms/live_surface.hpp"
#include "gui_forms/paint_framebuffer.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace gui_forms {

class Theme;
namespace detail {
struct SubSurfaceChannel;
}

// What a sub-surface shows (stage S2, threaded sub-surfaces design note
// section 2). The parent hands one to a SubSurfaceThread, which moves it to
// the sub-surface's thread; it is called and destroyed there, so the
// controls it makes are created on, owned by and released on that thread.
class SubSurfaceContent {
public:
    virtual ~SubSurfaceContent() = default;
    // Called once, on the sub-surface's thread, before the first
    // configuration is applied: the root control of the sub-surface's tree,
    // made on that thread. A null root or an exception stops the
    // sub-surface as failed (SubSurfaceState::failed).
    [[nodiscard]] virtual Control::Ptr build_root() = 0;
};

// The state both sides agree on in one visible frame (design note section
// 6), sent by the parent with each content epoch. Move-only: the
// framebuffer goes with it.
struct SubSurfaceConfiguration final {
    // Grows with every configuration the parent sends; each frame the
    // sub-surface publishes at this configuration carries it as its
    // LiveSurface content tag.
    std::uint64_t content_epoch{};
    // The region's logical size and the window scale.
    Size logical_size{};
    double scale{1.0};
    // The device size: ceil(logical_size * scale) on each axis, the size of
    // the framebuffer and of the LiveSurface (resize rule 2: composition is
    // a copy, never a scale).
    std::uint32_t pixel_width{};
    std::uint32_t pixel_height{};
    // Every pixel the sub-surface publishes has alpha 255.
    bool opaque{};
    // Made by the parent on its own thread (SurfaceRoot::create_framebuffer)
    // at the device size, and owned from then on by the sub-surface's
    // thread, which alone uses and destroys it. Null keeps the previous
    // framebuffer; a sub-surface without one lays out but publishes nothing.
    std::unique_ptr<PaintFramebuffer> framebuffer{};
};

// Where a sub-surface is in its life, as its parent last saw it.
enum class SubSurfaceState : std::uint8_t {
    // The thread is building the content's tree.
    starting,
    // The tree is built and the loop runs.
    running,
    // The loop ended after stop(); the tree is released.
    stopped,
    // The content refused (a null root or an exception); the thread ended.
    failed,
};

// Counters of one sub-surface (design note section 8, rules), written by its
// thread and read by its parent under the channel's lock.
struct SubSurfaceStatistics final {
    std::uint64_t configurations_received{};
    // Configurations replaced by a newer one before the thread applied them
    // (newest wins: resize rule 3).
    std::uint64_t configurations_coalesced{};
    std::uint64_t configurations_applied{};
    // LiveSurface reconfigures: device-size changes (resize cost A).
    std::uint64_t surface_reconfigures{};
    std::uint64_t frames_published{};
    // Paints whose frame waited because every LiveSurface buffer was held;
    // the damage is published with the next frame.
    std::uint64_t publish_retries{};
    // The content epoch of the newest published frame.
    std::uint64_t published_content_epoch{};
};

// A SurfaceRoot whose output is a LiveSurface and whose host is a control of
// its parent window (design note section 2). Made, run and destroyed by a
// SubSurfaceThread on the thread it owns; every member is that thread's
// (SurfaceRoot::check_access). Controls inside see window() null.
class SubSurface final : public SurfaceRoot {
public:
    // Binds the root to the calling thread and attaches `root`, which must
    // be alive and without a parent (std::invalid_argument or
    // std::logic_error otherwise). `resources` must be made on this thread.
    SubSurface(Control::Ptr root, std::shared_ptr<ApplicationResources> resources);
    ~SubSurface();
    SubSurface(const SubSurface&) = delete;
    SubSurface& operator=(const SubSurface&) = delete;

    // The agreed size and scale of a configuration: the surface side of
    // Window::resize and Window::set_scale. Root thread only.
    void apply_size(Size logical_size);
    void apply_scale(double scale);
};

// The parent's side of one sub-surface: owns its thread (owner decision 9,
// the library's thread) and the channel the two sides share. Made, used and
// destroyed on the parent's thread. Nothing crosses but the configurations,
// the stop request and the published frames (house style 8).
class SubSurfaceThread final {
public:
    // Starts the thread. It makes ApplicationResources of its own showing
    // `theme`, calls content.build_root(), makes a SubSurface of the root,
    // and waits for the first configuration: it produces nothing before the
    // first agreed size (resize rule 5). It publishes into `surface`,
    // reconfiguring it whenever the device size changes. `text_measure`, if
    // not null, is a framebuffer made by the parent whose painter measures
    // the sub-surface's text for its whole life (any size; one pixel will
    // do); without one, text is measured by the estimate. Null content,
    // surface or theme is std::invalid_argument; a thread the system
    // refuses is std::system_error.
    SubSurfaceThread(std::unique_ptr<SubSurfaceContent> content,
                     std::shared_ptr<LiveSurface> surface,
                     std::shared_ptr<const Theme> theme,
                     std::unique_ptr<PaintFramebuffer> text_measure);
    // Stops and joins (stop()).
    ~SubSurfaceThread();
    SubSurfaceThread(const SubSurfaceThread&) = delete;
    SubSurfaceThread& operator=(const SubSurfaceThread&) = delete;

    // Hands the sub-surface a new configuration and wakes it. Only the
    // newest unapplied one is kept (configurations_coalesced counts the
    // others). An epoch not above the last one sent is std::invalid_argument;
    // after stop() the configuration is released and nothing changes.
    void configure(SubSurfaceConfiguration configuration);
    // Revokes the sub-surface (it publishes no frame from now on), asks its
    // loop to stop, and joins the thread: the loop finishes its current
    // flush, releases its tree on its own thread and exits (design note
    // section 9). Idempotent.
    void stop() noexcept;
    // Posts to the sub-surface's thread. Empty (posting throws, as an empty
    // WindowDispatcher does) until the state is running; any thread may keep
    // the handle after the sub-surface stopped, and posts then fail as
    // posts to a closed window do.
    [[nodiscard]] WindowDispatcher dispatcher() const;
    [[nodiscard]] SubSurfaceState state() const;
    // What the content threw, for state failed; empty otherwise.
    [[nodiscard]] std::string failure() const;
    [[nodiscard]] SubSurfaceStatistics statistics() const;

private:
    std::shared_ptr<detail::SubSurfaceChannel> channel_;
    std::thread thread_;
    std::uint64_t last_content_epoch_{};
};

} // namespace gui_forms
