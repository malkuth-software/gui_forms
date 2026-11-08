#pragma once

#include "gui_forms/host/types/host_types.hpp"
#include "gui_forms/window.hpp"
#include "tool_tip_bubble.hpp"
#include "tool_tip_route.hpp"
#include "tool_tip_subjects.hpp"

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace gui_forms::detail {

class ToolTipBubble;
class ToolTipLayer;
class TransientWindow;

// A window's one tooltip presenter (owner, 2026-10-28). The window owns it
// and feeds it pointer motion, presses, focus changes, handled keys and
// layout; it reads the window's switches and settings, finds the thing
// under the pointer or under focus through the controls' tooltip text and
// their tooltip items (Control::tool_tip_items), and shows that thing's text in a bubble
// overlay it owns.
//
// States (one at a time, in `state_`):
// - Idle: nothing waits and nothing shows.
// - Waiting: a tooltip may show once the pointer has rested, or once focus
//   has stayed, for `delay`; a wake is armed for then.
// - Settled: the pointer rested over a region with nothing to show; nothing
//   waits until it leaves `region`.
// - Showing: a bubble shows; a wake is armed for its auto-pop when it has
//   one.
// Any input a state does not name is ignored for that state.
//
// Every entry point runs on the window's UI thread. The visibility event is
// raised last in each operation, after the state is complete, so a handler
// may call back in.
class ToolTipPresenter final : public ToolTipRoute {
public:
    explicit ToolTipPresenter(Window& window) noexcept;
    ~ToolTipPresenter() override;
    ToolTipPresenter(const ToolTipPresenter&) = delete;
    ToolTipPresenter& operator=(const ToolTipPresenter&) = delete;

    // Window input, after the window's own routing.
    void pointer_moved(Point position, const Control::Ptr& hovered, FrameTime now) override;
    void pointer_pressed() override;
    void pointer_left() override;
    void focus_changed(const Control::Ptr& focused) override;
    void key_handled() override;
    // Called after the window arranged its root, before its overlays: a bubble
    // below a control follows the control.
    void root_arranged() override;
    void text_changed(const Control& control) override;
    // A part of `control` that shows closes when it is gone, and shows its
    // new text when that changed; a wait or a settled region over `control`
    // is found again, as if the pointer had just come to rest where it
    // rested (a sub-surface's request arrives after the pointer reached its
    // host); the thing under focus is found again when `control` has focus.
    void items_changed(const Control& control) override;
    void settings_changed() override;
    void cover(RuntimeId control, std::string_view item_id, bool covered) override;

    void show_program(const Control::Ptr& target, std::optional<std::chrono::milliseconds> duration) override;
    void show_program_item(const Control::Ptr& control, std::string item_id, std::string text, Rect anchor,
                           std::optional<std::chrono::milliseconds> duration) override;
    void request(const Control::Ptr& owner, std::uint64_t id, std::string item_id,
                 const ToolTipRequest& content) override;
    void withdraw(std::string_view item_id) override;
    void hide() override;
    // Closes the bubble without raising the visibility event and drops every
    // wake; the presenter stays Idle. The window's destructor calls it first.
    void shutdown() noexcept override;

    [[nodiscard]] bool visible() const noexcept override;
    [[nodiscard]] Control::Ptr target() const noexcept override;
    [[nodiscard]] std::string item_id() const override;
    [[nodiscard]] std::string text() const override;

private:
    // The bubble that shows and the overlay that holds it. Closing it (its
    // destructor) disconnects the closed-event subscription first, then the
    // overlay, and releases the controls last: the window detaches the overlay's
    // tree while they still live, so no stale stable ID remains.
    class Overlay final {
    public:
        Overlay(std::shared_ptr<ToolTipLayer> layer_control, std::shared_ptr<ToolTipBubble> bubble_control,
                OverlayToken overlay_token) noexcept;
        ~Overlay();
        Overlay(const Overlay&) = delete;
        Overlay& operator=(const Overlay&) = delete;

        std::shared_ptr<ToolTipLayer> layer;
        std::shared_ptr<ToolTipBubble> bubble;
        OverlayToken token;
        SubscriptionToken closed;
    };

    struct Idle final {};
    struct Waiting final {
        ToolTipOrigin origin{ToolTipOrigin::pointer};
        std::chrono::milliseconds delay{};
        // The thing that will show, as found when the wait began.
        ToolTipSubject subject{};
        // For the pointer, the control it was over.
        std::weak_ptr<Control> hovered{};
    };
    struct Settled final {
        Rect region{};
        // The control the pointer was over; moving onto another control
        // leaves the region too.
        std::weak_ptr<Control> hovered{};
    };
    // A bubble shows either in the window's tooltip Splash window
    // (`overlay` null: the presenter's kept bubble in transient_) or, where
    // the run cannot open one, as an overlay in the window.
    struct Showing final {
        ToolTipOrigin origin{ToolTipOrigin::pointer};
        ToolTipSubject subject{};
        std::unique_ptr<Overlay> overlay{};
    };
    using State = std::variant<Idle, Waiting, Settled, Showing>;

    // The wake's target: runs on the UI thread from the window's frame
    // schedule.
    struct Wake final {
        ToolTipPresenter* presenter{};

        void operator()(FrameTime now) const;
    };
    // The overlay closed under the presenter (its owner left the window or
    // was disposed).
    struct OverlayClosed final {
        ToolTipPresenter* presenter{};

        void operator()() const;
    };
    // The tooltip Splash window closed under the presenter (its owner
    // closed, or the host destroyed it).
    struct TransientClosed final {
        ToolTipPresenter* presenter{};

        void operator()() const;
    };

    [[nodiscard]] bool hover_allowed() const noexcept;
    [[nodiscard]] bool focus_allowed() const noexcept;
    [[nodiscard]] std::chrono::milliseconds show_delay(FrameTime now) const noexcept;
    void wait(ToolTipOrigin origin, ToolTipSubject subject, FrameTime now);
    void arm(FrameTime deadline);
    void woke(FrameTime now);
    void overlay_closed();
    void transient_closed();
    // The tooltip Splash window, opened on first use and kept (warm
    // standby, APP_WINDOW_RULES_DESIGN Part S); null when the run cannot
    // open one now, and for good after the host refused one.
    [[nodiscard]] TransientWindow* splash_window();
    // Shows `subject` in the Splash window with the kept bubble, its lines
    // in `lines` (swapped into the bubble). False, changing nothing that
    // shows, when there is no Splash window or the host refused.
    [[nodiscard]] bool show_in_splash(Showing& showing, ToolTipLines& lines);
    // Opens the bubble for `subject`; leaves the presenter Idle when the
    // subject cannot show (detached, or disabled without show_when_disabled).
    // Returns the visibility event to raise, or none.
    [[nodiscard]] std::optional<ToolTipEvent> open(ToolTipOrigin origin, ToolTipSubject subject,
                                                   std::optional<std::chrono::milliseconds> duration);
    // Closes a bubble that shows, entering `next`. Returns the visibility
    // event to raise, or none when nothing showed.
    [[nodiscard]] std::optional<ToolTipEvent> close(State next);
    // Closes what shows (raising its event), then opens `subject` unless a
    // handler of that event showed another tooltip, which then stands.
    void replace(ToolTipOrigin origin, ToolTipSubject subject,
                 std::optional<std::chrono::milliseconds> duration);
    // Places the bubble that shows; false when the Splash window refused it.
    bool position(Showing& showing);
    void raise(const std::optional<ToolTipEvent>& change);
    // The pointer moved with nothing showing for it: wait, settle or idle.
    void pointer_wait(const Control::Ptr& hovered, FrameTime now);

    Window& window_;
    State state_{Idle{}};
    FrameRequestToken wake_{};
    Point pointer_{};
    FrameTime last_motion_{};
    FrameTime last_closed_{};
    // The thing keyboard focus last named (shown, waiting or with nothing to
    // show), so a handled key that leaves focus on it changes nothing.
    ToolTipSubject keyboard_subject_{};
    std::uint64_t next_overlay_{1U};
    ToolTipCovers covers_{};
    // The warm tooltip Splash window and the bubble kept for it, with the
    // lines storage swapped in and out of the bubble on each show. Declared
    // after what they borrow from (the window) and destroyed by shutdown().
    std::unique_ptr<TransientWindow> transient_{};
    SubscriptionToken transient_closed_{};
    std::shared_ptr<ToolTipLayer> transient_layer_{};
    std::shared_ptr<ToolTipBubble> transient_bubble_{};
    ToolTipLines transient_lines_{};
    // The monitor table's storage for Splash placement, reused by every show.
    HostMonitorResult placement_monitors_{};
    // The host refused a Splash window once; the overlay is used from then.
    bool transient_refused_{};
};

} // namespace gui_forms::detail
