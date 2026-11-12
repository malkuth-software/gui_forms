#pragma once
// The Windows Forms key event records a control and a window raise
// (Control::preview_key_down, key_down, key_press, key_up; the window's
// key_down, key_press, key_up and key_state_changed), and the portable
// Windows Forms key code of a physical key.
//
// Order of one key press, as SurfaceRoot::dispatch_key and dispatch_text
// raise it:
//   1. key_state_changed on the window: the raw press, first down only
//      (never for an auto-repeat), before anything can consume the key;
//   2. the window's key rules (a consuming rule ends the key);
//   3. preview_key_down on the focused control (downs only): setting
//      is_input_key keeps the key from dialog processing (step 8);
//   4. the window's command keys (ProcessCmdKey: accelerators);
//   5. key_down (or key_up) on the window (Windows Forms' KeyPreview);
//   6. the controls' internal preview route, then key_down (or key_up) on
//      the focused control (Control::on_key_down or on_key_up: its base
//      raises the event, and a control's own key handling runs in its
//      override after the base, unless a subscriber set handled), then the
//      bubble route;
//   7. key_press with the text the key produced (TextInputEvent), on the
//      window and then the focused control, unless a key_down set
//      suppress_key_press; text still being composed (an input method's
//      marked text) raises no key_press and goes to the control untouched;
//   8. for a key nothing handled: accelerators, then dialog keys (Enter,
//      Escape, mnemonics: ProcessDialogKey) and focus traversal (Tab, F6).
// A handler that sets handled ends the key's route at that step. The
// window losing activation releases every held key: key_state_changed
// with down false and released_by_deactivation, and key_up on the window
// and the focused control with released_by_deactivation set.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/events/input_events/input_events.hpp"

#include <cstdint>
#include <string>

namespace gui_forms {

// One key down or up as Windows Forms' KeyEventArgs gives it. key_code is
// the Windows Forms Keys code (Windows virtual-key numbering, the same on
// every host: key_code_from_physical_key), physical_key the USB HID usage
// the host reported (PhysicalKey), scan_code the host's own hardware code
// (a Windows scan code, 0xE000 added for an extended key; an X11 keycode;
// an AppKit keyCode; zero when the host gave none or the key was released
// by deactivation). repeat marks an auto-repeated down. A handler sets
// handled to end the key's route; suppress_key_press also drops the text
// the key produces (it implies handled).
struct GUI_FORMS_API KeyEventArgs final {
    std::uint32_t key_code{};
    std::uint32_t physical_key{};
    std::uint32_t scan_code{};
    Modifier modifiers{Modifier::none};
    bool repeat{};
    bool released_by_deactivation{};
    bool handled{};
    bool suppress_key_press{};
};

// The routed key event a KeyEventArgs carries, for code that takes the
// routed form (an editing engine, a key command table): action is the
// raiser's (down for on_key_down, up for on_key_up); the phase is target.
// The caller copies handled back.
[[nodiscard]] inline KeyEvent key_event_of(const KeyEventArgs& arguments, KeyAction action) noexcept {
    KeyEvent event{};
    event.action = action;
    event.physical_key = arguments.physical_key;
    event.modifiers = arguments.modifiers;
    event.phase = EventPhase::target;
    event.repeat = arguments.repeat;
    event.handled = arguments.handled;
    event.scan_code = arguments.scan_code;
    return event;
}

// Windows Forms' PreviewKeyDownEventArgs: raised on the focused control
// before anything else sees a key down. A handler sets is_input_key to
// keep the key from dialog processing (Tab, Enter, Escape, mnemonics), so
// that the control receives it as an ordinary key.
struct GUI_FORMS_API PreviewKeyDownEventArgs final {
    std::uint32_t key_code{};
    std::uint32_t physical_key{};
    Modifier modifiers{Modifier::none};
    bool repeat{};
    bool is_input_key{};
};

// Windows Forms' KeyPressEventArgs for committed text input: text is what
// the input produced (UTF-8; one character for a typed key, several for a
// committed composition or a paste the host sends as text), key_char its
// one Unicode scalar value when it holds exactly one, else zero. A handler
// sets handled to keep the text from the control.
struct GUI_FORMS_API KeyPressEventArgs final {
    std::string text;
    std::uint32_t key_char{};
    bool handled{};
};

// A physical key going down or up, raw: raised once per press and once
// per release (never for an auto-repeat), before any route can consume
// the key and whatever control has the focus, so that a game can keep
// key state without text input or focus in the way. down is false for a
// release; released_by_deactivation marks the releases the window makes
// when it loses activation (the host's own key-up never arrives then).
struct GUI_FORMS_API KeyStateChange final {
    std::uint32_t key_code{};
    std::uint32_t physical_key{};
    std::uint32_t scan_code{};
    Modifier modifiers{Modifier::none};
    bool down{};
    bool released_by_deactivation{};
};

// The Windows Forms Keys code of a physical key (a USB HID usage): the
// key's code on a US layout, the same on every host. The modifier keys
// give the side-less codes Windows Forms reports in KeyDown (ShiftKey
// 0x10, ControlKey 0x11, Menu 0x12; the Windows keys 0x5B and 0x5C).
// Zero for a usage with no Keys code.
[[nodiscard]] GUI_FORMS_API std::uint32_t key_code_from_physical_key(std::uint32_t physical_key) noexcept;

// The single Unicode scalar value UTF-8 `text` holds, or zero when it
// holds none, more than one, or is not valid UTF-8.
[[nodiscard]] GUI_FORMS_API std::uint32_t single_key_char(const std::string& text) noexcept;

} // namespace gui_forms
