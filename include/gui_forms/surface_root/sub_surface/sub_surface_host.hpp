#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/surface_root/sub_surface/sub_surface.hpp"

#include <cstdint>
#include <memory>

namespace gui_forms {

// The parent's control for one threaded sub-surface (stage S2, design note
// section 2): it occupies the region, owns the sub-surface's thread, sizes
// and reconfigures it, and composes its LiveSurface into the window. On the
// window's dirty structure it is one control; the sub-surface's frames never
// mark the window's tree. UI thread, like every control of its window.
//
// Life: the content starts on its own thread the first time the host is
// attached to a Window with a non-empty size (nothing is produced before
// the first agreed size, resize rule 5), and stops when the host leaves
// the window: the thread releases the content and is joined. A host that
// is attached again shows nothing until it is given content again.
class SubSurfaceHost final : public Control {
public:
    explicit SubSurfaceHost(StableId stable_id);
    ~SubSurfaceHost() override;

    // The content to show and whether it covers its whole region with
    // opaque pixels (the window then copies its frames instead of blending,
    // resize rule 2). A running sub-surface is stopped and joined first.
    // Null content stops it and leaves the region empty.
    void set_content(std::unique_ptr<SubSurfaceContent> content, bool opaque);
    [[nodiscard]] bool opaque() const noexcept { return opaque_; }
    // Whether the sub-surface's thread exists (started and not stopped).
    [[nodiscard]] bool started() const noexcept { return static_cast<bool>(thread_); }
    // The thread's state; stopped while there is none.
    [[nodiscard]] SubSurfaceState state() const;
    [[nodiscard]] SubSurfaceStatistics statistics() const;
    // Posts to the sub-surface's thread (SubSurfaceThread::dispatcher);
    // empty while it is not running.
    [[nodiscard]] WindowDispatcher dispatcher() const;
    // The content epoch of the newest configuration sent (design note
    // section 6); zero before the first.
    [[nodiscard]] std::uint64_t content_epoch() const noexcept { return content_epoch_; }
    // The surface the sub-surface publishes into; null before it started.
    [[nodiscard]] std::shared_ptr<LiveSurface> live_surface() const noexcept { return surface_; }

    // Arranges as any control, then reconfigures the sub-surface when its
    // device size or scale changed. The window's theme and presentation
    // settings travel with every configuration, and a change of either
    // sends one.
    void arrange(Rect final_bounds) override;
    // Composes the newest frame into the region (Painter::draw_live_surface).
    void on_paint(Painter& painter, Rect local_damage) override;

protected:
    void on_attached_to_window() override;
    void on_detaching_from_window(SurfaceRoot& former_window) noexcept override;

private:
    // Starts the thread when it can, and sends a configuration when the
    // region's device size or the window scale changed: a new content epoch,
    // a framebuffer at the new size, the pairing requirement, and a paint of
    // the region so the change is presented without waiting for an expose
    // (resize rule 3).
    void configure_region();
    void stop_thread() noexcept;
    // The window's theme or presentation settings changed: the next
    // configuration carries them under a new content epoch.
    void on_window_theme_changed(const Theme& theme);
    void on_window_presentation_changed(const PresentationSettings& settings);

    std::unique_ptr<SubSurfaceContent> content_{};
    std::shared_ptr<LiveSurface> surface_{};
    std::unique_ptr<SubSurfaceThread> thread_{};
    std::uint64_t content_epoch_{};
    // The device size and scale of the last configuration sent.
    std::uint32_t pixel_width_{};
    std::uint32_t pixel_height_{};
    double configured_scale_{};
    bool opaque_{};
    // An environment change not yet sent.
    bool environment_changed_{};
    SubscriptionToken theme_subscription_{};
    SubscriptionToken presentation_subscription_{};
};

} // namespace gui_forms
