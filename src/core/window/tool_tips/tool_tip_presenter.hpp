#pragma once

#include "gui_forms/window.hpp"

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

// What showed a tooltip: the pointer resting over its thing, keyboard focus
// reaching it, or the program (Window::show_tool_tip).
enum class ToolTipOrigin : std::uint8_t { pointer, keyboard, program };

// The thing a tooltip belongs to: a control, or one authored part of it
// (`item_id`, its semantic virtual child's stable ID). `anchor` is the
// thing's bounds in window coordinates. An empty `text` means the thing
// shows no tooltip; `anchor` is then the region over which nothing shows.
struct ToolTipSubject final {
    std::weak_ptr<Control> control{};
    std::string item_id{};
    std::string text{};
    Rect anchor{};
};

// A window's one tooltip presenter (owner, 2026-10-28). The window owns it
// and feeds it pointer motion, presses, focus changes, handled keys and
// layout; it reads the window's switches and settings, finds the thing
// under the pointer or under focus through the controls' tooltip text and
// their semantic virtual children, and shows that thing's text in a bubble
// popup it owns.
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
class ToolTipPresenter final {
public:
    explicit ToolTipPresenter(Window& window) noexcept;
    ~ToolTipPresenter();
    ToolTipPresenter(const ToolTipPresenter&) = delete;
    ToolTipPresenter& operator=(const ToolTipPresenter&) = delete;

    // Window input, after the window's own routing.
    void pointer_moved(Point position, const Control::Ptr& hovered, FrameTime now);
    void pointer_pressed();
    void pointer_left();
    void focus_changed(const Control::Ptr& focused);
    void key_handled();
    // Called after the window arranged its root, before its popups: a bubble
    // below a control follows the control.
    void root_arranged();
    void text_changed(const Control& control);
    void settings_changed();
    void cover(RuntimeId control, std::string_view item_id, bool covered);

    void show_program(const Control::Ptr& target, std::optional<std::chrono::milliseconds> duration);
    void hide();
    // Closes the bubble without raising the visibility event and drops every
    // wake; the presenter stays Idle. The window's destructor calls it first.
    void shutdown() noexcept;

    [[nodiscard]] bool visible() const noexcept;
    [[nodiscard]] Control::Ptr target() const noexcept;
    [[nodiscard]] std::string item_id() const;
    [[nodiscard]] std::string text() const;

private:
    // The bubble that shows and the popup that holds it. Closing it (its
    // destructor) disconnects the closed-event subscription first, then the
    // popup, and releases the controls last: the window detaches the popup's
    // tree while they still live, so no stale stable ID remains.
    class Overlay final {
    public:
        Overlay(std::shared_ptr<ToolTipLayer> layer_control, std::shared_ptr<ToolTipBubble> bubble_control,
                PopupToken popup_token) noexcept;
        ~Overlay();
        Overlay(const Overlay&) = delete;
        Overlay& operator=(const Overlay&) = delete;

        std::shared_ptr<ToolTipLayer> layer;
        std::shared_ptr<ToolTipBubble> bubble;
        PopupToken token;
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
    // The popup closed under the presenter (its owner left the window or
    // was disposed).
    struct PopupClosed final {
        ToolTipPresenter* presenter{};

        void operator()() const;
    };

    [[nodiscard]] bool hover_allowed() const noexcept;
    [[nodiscard]] bool focus_allowed() const noexcept;
    [[nodiscard]] bool covered(const Control& control, std::string_view item_id) const;
    [[nodiscard]] bool available(const Control& control) const noexcept;
    [[nodiscard]] ToolTipSubject resolve_pointer(const Control::Ptr& hovered, Point position) const;
    [[nodiscard]] ToolTipSubject resolve_focus(const Control::Ptr& focused) const;
    [[nodiscard]] bool same_thing(const ToolTipSubject& left, const ToolTipSubject& right) const noexcept;
    [[nodiscard]] std::chrono::milliseconds show_delay(FrameTime now) const noexcept;
    void wait(ToolTipOrigin origin, ToolTipSubject subject, FrameTime now);
    void arm(FrameTime deadline);
    void woke(FrameTime now);
    void popup_closed();
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
    void position(Showing& showing);
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
    // Covers by (control runtime ID, item ID), counted.
    std::map<std::pair<std::uint64_t, std::string>, std::size_t> covers_{};
};

} // namespace gui_forms::detail
