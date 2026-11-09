#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/types/geometry/geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace gui_forms {

class Window;

namespace detail {
class PopupLiftFrame;
} // namespace detail

// Where the keyboard goes while a lift shows: `forwarded` gives the content
// the Popup's focus and offers it the owner's keys and text first; with
// `kept_by_owner` the owner's focused control keeps them (a drop-down field
// whose editor drives its list).
enum class PopupLiftInput : std::uint8_t {
    forwarded,
    kept_by_owner,
};

// A drop-down's list or a picker's panel lifted out of its owner window
// into the owner's warm Popup window (owner, 2026-11-08:
// APP_WINDOW_RULES_DESIGN Part S, drop-downs and pickers are Popups before
// 0.5), where it may extend past the owner's edges onto the desktop. The control keeps its
// overlay layer in the owner (it takes outside clicks and holds the owner's
// focus) and lifts only the content: lift shows `content` in
// Window::picker_popup_window_slot at `face`, framed with room for the
// theme's overlay shadow when the Popup is translucent, gives it the Popup's
// focus and forwards the owner's keys and text to it
// (Window::forward_input_to_popup_window). Keys it leaves unhandled (Escape)
// reach the owner's layer as before. release hides the Popup, takes the
// content out (detached again) and ends the forwarding; the destructor
// releases. One lift per object; a second lift replaces the first. UI
// thread only, on the owner window's thread.
class PopupLift final {
public:
    PopupLift() noexcept;
    ~PopupLift();
    PopupLift(const PopupLift&) = delete;
    PopupLift& operator=(const PopupLift&) = delete;

    // Shows `content` (a live control with no parent and in no window) in
    // `window`'s picker Popup at `face`, the content's bounds in the
    // window's client coordinates (which may reach past the client area;
    // the Popup is kept inside the monitor's work area). False, changing
    // nothing, when the run has no Popup windows or the host refuses: the
    // caller then shows the content in its overlay layer.
    [[nodiscard]] bool lift(Window& window, const Control::Ptr& content, Rect face,
                            PopupLiftInput input = PopupLiftInput::forwarded);
    // Moves the lifted content to `face`; false when nothing is lifted.
    [[nodiscard]] bool move(Rect face);
    void release() noexcept;
    [[nodiscard]] bool lifted() const noexcept { return window_ != nullptr; }
    // The window the content shows in while lifted (the Popup's model), or
    // null.
    [[nodiscard]] Window* host() const noexcept;

private:
    // Observes the owner window while lifted; null otherwise.
    Window* window_{};
    std::shared_ptr<detail::PopupLiftFrame> frame_{};
};

// The face, in `window`'s client coordinates, of a drop-down of `size` hung
// from `anchor` (client coordinates) at `preferred_x`, against the work area
// of the monitor under the anchor's lower left corner: under the anchor, or
// above it when it would overrun the area's bottom, and moved and cut to stay
// inside the area. None when the run has no Popup windows or cannot say the
// work area; the caller then places the drop-down in its client. UI thread.
[[nodiscard]] std::optional<Rect> drop_down_popup_face(Window& window, Rect anchor, Size size, double preferred_x);

} // namespace gui_forms
