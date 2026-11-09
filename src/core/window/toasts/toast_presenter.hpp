#pragma once

#include "gui_forms/window.hpp"
#include "gui_forms/window/toasts/toasts.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

namespace gui_forms::detail {

// The full-window layer a toast sits in: drawn on the overlay plane, never
// focused, and hit only through its content (the layer itself lets the
// pointer through to the window beneath).
class ToastLayer final : public Control {
public:
    explicit ToastLayer(StableId stable_id);
    [[nodiscard]] bool hit_test_local(Point point) const override;
};

// A window's one toast presenter (W-4; coordinator ruling: built on the
// ToolTipPresenter's pattern so that both move to lightweight Splash
// windows together in Part S). It places a caller's control subtree, the
// toast's content, in an overlay it owns, centred above the bottom edge of
// the client area, for a duration; it holds no host code and draws nothing
// itself.
//
// States: Idle, or Showing one toast (`showing_`), with a wake armed for
// its duration when it has one. A new toast replaces the one that shows.
//
// Every entry point runs on the window's UI thread. The dismissal event is
// raised last in each operation, after the state is complete, so a handler
// may show another toast.
class ToastPresenter final {
public:
    explicit ToastPresenter(Window& window) noexcept;
    ~ToastPresenter();
    ToastPresenter(const ToastPresenter&) = delete;
    ToastPresenter& operator=(const ToastPresenter&) = delete;

    // Window::show_toast; the window validated its arguments. Returns the
    // new toast's ID.
    std::uint64_t show(const Control::Ptr& owner, const Control::Ptr& content, std::chrono::milliseconds duration);
    // Closes the toast that shows when its content is `content`; false,
    // changing nothing, otherwise.
    bool dismiss(const Control& content, ToastDismissReason reason);
    void hide();
    // The client area changed size: the toast is placed again.
    void client_resized();
    // Closes the toast without raising the event and drops the wake; the
    // window's destructor calls it first.
    void shutdown() noexcept;

    [[nodiscard]] bool visible() const noexcept;
    [[nodiscard]] Control::Ptr content() const noexcept;
    [[nodiscard]] std::uint64_t id() const noexcept;

private:
    // The layer, the content it holds and the overlay that shows them.
    // Closing (the destructor) disconnects the closed-event subscription,
    // then the overlay, then takes the content out of the layer, so that
    // the caller's control is detached and may be shown again.
    class Overlay final {
    public:
        Overlay(std::shared_ptr<ToastLayer> layer_control, Control::Ptr content_control,
                OverlayToken overlay_token) noexcept;
        ~Overlay();
        Overlay(const Overlay&) = delete;
        Overlay& operator=(const Overlay&) = delete;

        std::shared_ptr<ToastLayer> layer;
        Control::Ptr content;
        OverlayToken token;
        SubscriptionToken closed;
    };
    struct Showing final {
        std::uint64_t id{};
        std::unique_ptr<Overlay> overlay{};
    };

    // The wake's target: runs on the UI thread from the window's frame
    // schedule when the duration runs out.
    struct Wake final {
        ToastPresenter* presenter{};
        void operator()(FrameTime now) const;
    };
    // The overlay closed under the presenter (its owner left the window,
    // was hidden or was disposed).
    struct OverlayClosed final {
        ToastPresenter* presenter{};
        void operator()() const;
    };

    // Closes the toast that shows; returns its dismissal, or none when
    // nothing showed.
    [[nodiscard]] std::optional<ToastDismissal> close(ToastDismissReason reason);
    void position();
    void raise(const std::optional<ToastDismissal>& dismissal);

    Window& window_;
    std::optional<Showing> showing_{};
    FrameRequestToken wake_{};
    std::uint64_t next_id_{1U};
};

} // namespace gui_forms::detail
