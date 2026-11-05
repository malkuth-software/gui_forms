#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/surface_root/sub_surface/sub_surface.hpp"

#include <cstdint>
#include <memory>
#include <span>

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
//
// Input (design note section 4): the host is a tab stop. Pointer events that
// reach it (the window's capture holds a pressed pointer on it), and key and
// text events while it has keyboard focus, are forwarded in order to the
// sub-surface's thread, which dispatches them to its own tree; the window's
// accelerator pre-filter has already run. Keys the sub-surface leaves
// unhandled come back asynchronously and continue in the window as if the
// host had not handled them: bubble from the host's parent, accelerators,
// dialog keys, and Tab, which moves focus past the host because the
// sub-surface's own traversal does not wrap. Keyboard focus inside the
// sub-surface follows the host's focus and the window's activation.
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
    // Keys the sub-surface returned after the host had lost keyboard focus
    // or left its window; they are not replayed in the window.
    [[nodiscard]] std::uint64_t returned_keys_discarded() const noexcept { return returned_keys_discarded_; }

    // Arranges as any control, then reconfigures the sub-surface when its
    // device size or scale changed. The window's theme and presentation
    // settings travel with every configuration, and a change of either
    // sends one.
    void arrange(Rect final_bounds) override;
    // Composes the newest frame into the region (Painter::draw_live_surface).
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // A pane of its own, named by the accessible name ("Sub-surface"
    // without one), whose default action focuses it: the host is the tab
    // stop through which focus enters the sub-surface. The sub-surface's
    // tree publishes its nodes in its own root, not under this one.
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    // Stage T1 anatomy, as dot members (see Control::Parts). `content` is
    // the composed sub-surface frame, present once the sub-surface has a
    // LiveSurface; the sub-surface's own controls are parts of its tree,
    // not of the host.
    struct Parts final {
        PartGeometry region;
        PartGeometry content;
        PartGeometry focus;
    };
    struct States final {
        bool normal{};
        bool enabled{};
        bool disabled{};
        bool focused{};
        bool focus_visible{};
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] States states() const;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void on_attached_to_window() override;
    void on_detaching_from_window(SurfaceRoot& former_window) noexcept override;
    // Forward to the sub-surface while it runs and mark the event handled.
    void on_pointer(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;
    void on_text_input(TextInputEvent& event) override;
    void on_focus_changed(bool focused) override;
    // The sub-surface's published caret: its coordinates are the host's.
    [[nodiscard]] std::optional<Rect> text_input_caret() const override;

private:
    // The sub-surface's key return target for one thread. The host owns it
    // and revokes it when that thread stops; a key posted before then finds
    // it revoked or released and is dropped. Parent thread only.
    class KeyReturn;

    // Tells the sub-surface whether it has keyboard focus: the host is the
    // window's focused control in an active window. `entry` is where focus
    // goes when it enters.
    void update_keyboard_focus(SubSurfaceFocusEntry entry);
    void on_window_active_changed(bool active);
    // Takes or releases the window's pointer capture for a forwarded press
    // or release (pressed_buttons_).
    void hold_capture_for(const PointerEvent& event);
    // A key the sub-surface returned (KeyReturn): continues in the window.
    void continue_returned_key(const KeyEvent& event);
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
    SubscriptionToken active_subscription_{};
    std::shared_ptr<KeyReturn> key_return_{};
    // The sub-surface was last told it has keyboard focus.
    bool keyboard_inside_{};
    // The non-primary pointer buttons forwarded down and not yet up, one bit
    // per PointerButton. While any is down the host holds the window's
    // pointer capture, so a drag that leaves the region still reaches the
    // sub-surface (design note section 4); the window itself captures only
    // for the primary button.
    std::uint8_t pressed_buttons_{};
    // The host, not the window, took the capture pressed_buttons_ holds.
    bool took_capture_{};
    std::uint64_t returned_keys_discarded_{};
};

} // namespace gui_forms
