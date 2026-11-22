#pragma once
// Drag and drop with other programs on Win32 (OLE): the window as a drop
// target (RegisterDragDrop with an IDropTarget) and drags from the program
// (DoDragDrop with an IDropSource and an IDataObject). Both run on the
// window's UI thread, a single-threaded apartment the host enters with
// CoInitializeEx; each OLE user here also holds its own OleInitialize, which
// OLE drag and drop needs and which nests inside that apartment. The model
// for formats and effects is the X11 host's XDND destination
// (linux_drag_drop.cpp) and the AppKit host's view: file lists, Unicode text
// and media-typed bytes, with the copy, move and link effects, whose OLE
// values are the portable ones.
//
// Not compiled in a high-integrity build (cmake/HighIntegrity.cmake).

#include "gui_forms/host.hpp"

#include <windows.h>
#include <ole2.h>

#include <cstdint>
#include <vector>

namespace gui_forms::host::detail {
#if !defined(GUI_FORMS_HIGH_INTEGRITY)

// The portable effects of OLE effect bits; bits OLE has beyond link (the
// scroll bit) are dropped.
[[nodiscard]] DragEffect windows_drag_effects(DWORD effects) noexcept;
// The OLE effect bits of portable effects.
[[nodiscard]] DWORD windows_drop_effects(DragEffect effects) noexcept;
// The portable modifiers of an OLE key state (MK_SHIFT, MK_CONTROL, MK_ALT).
[[nodiscard]] Modifier windows_drag_modifiers(DWORD key_state) noexcept;

// The typed items a drag's data offers, read once: its file list (CF_HDROP),
// its Unicode text (CF_UNICODETEXT) and its PNG bytes (the registered "PNG"
// format, as "image/png"), in that order, each kept to DragLimits. A format
// that is absent, unreadable or over its bound is left out; an empty result
// means the drag offers nothing the program reads.
[[nodiscard]] std::vector<DragDataItem> windows_drag_items(IDataObject& data);

// A drag's data for DoDragDrop, holding `items` as OLE formats: a file list
// as CF_HDROP, text as CF_UNICODETEXT, bytes as the clipboard format their
// media type registers ("image/png" as "PNG"). The first item of each
// format is offered; a later one of the same format is not. Returned with
// one reference the caller owns (Release), or nullptr when the items
// cannot be converted (a path or text that is not UTF-8, or no memory).
[[nodiscard]] IDataObject* make_windows_drag_data(const std::vector<DragDataItem>& items);

// The window side of the drop target: the host translates each OLE call
// into a DragEvent for its session. `client` is the pointer in the window's
// client device pixels; the host sets the event's position from it. The
// answer is the effect the window accepts (none refuses); for a drop, the
// effect the window took.
class WindowsDragDispatch {
public:
    [[nodiscard]] virtual DragEffect dispatch_external_drag(DragEvent event, POINT client) = 0;

protected:
    WindowsDragDispatch() = default;
    ~WindowsDragDispatch() = default;
    WindowsDragDispatch(const WindowsDragDispatch&) = default;
    WindowsDragDispatch& operator=(const WindowsDragDispatch&) = default;
};

class WindowsDropTarget;

// One window's registration as a drop target: OleInitialize and
// RegisterDragDrop on register_window, RevokeDragDrop and OleUninitialize
// on revoke or destruction. Revoke before the window is destroyed (its
// WM_DESTROY); the target then stops calling `dispatch`, which must outlive
// the registration. UI thread.
class WindowsDropRegistration final {
public:
    WindowsDropRegistration() = default;
    ~WindowsDropRegistration();
    WindowsDropRegistration(const WindowsDropRegistration&) = delete;
    WindowsDropRegistration& operator=(const WindowsDropRegistration&) = delete;

    // False, registering nothing, when OLE cannot start on this thread or
    // the window refuses registration; a window registers once.
    [[nodiscard]] bool register_window(HWND window, WindowsDragDispatch& dispatch);
    void revoke() noexcept;
    [[nodiscard]] bool registered() const noexcept { return target_ != nullptr; }

private:
    HWND window_{};
    WindowsDropTarget* target_{};
    bool ole_initialized_{};
};

// How a drag from the program ended: the effect the destination took (none
// when cancelled), or the failure that kept it from starting.
struct WindowsDragOutcome final {
    HostServiceStatus status;
    DragEffect performed_effect{DragEffect::none};
};

// Runs a drag of `request` (validated by HostServices::start_drag) through
// DoDragDrop and returns when it ends. The drag follows the mouse buttons
// held when it starts and drops when they are all released; Escape cancels.
// backend_failure when no button is held, OLE cannot start, or the data
// cannot be built. UI thread.
[[nodiscard]] WindowsDragOutcome run_windows_drag(const HostDragSourceRequest& request);

#endif
} // namespace gui_forms::host::detail
