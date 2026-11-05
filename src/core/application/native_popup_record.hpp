#pragma once

#include "gui_forms/application/application.hpp"
#include "gui_forms/window/native_popup/native_popup.hpp"

#include <cstdint>
#include <memory>

namespace gui_forms::detail {

struct ApplicationWindowState;

// One native popup window (SurfaceRoot::open_native_popup): the run's
// handle on it and a borrowed pointer to its model. Shared by the token
// that keeps it open and, as a revocable, by the control that opened it.
// The window and its model are owned by the host's entry; the model
// pointer is used only while the handle is active, because the host
// destroys the model only after the window's state is revoked.
class NativePopupRecord final : public Revocable {
public:
    NativePopupRecord(ApplicationWindowHandle window, Window* model) noexcept;

    // Destroys the popup window if it is still open (ApplicationWindows::
    // destroy_window): its closed callback runs before this returns.
    void disconnect() noexcept override;
    [[nodiscard]] bool connected() const noexcept override;
    // The popup's model while the window is open, else null.
    [[nodiscard]] Window* model() const noexcept;
    // The popup window's identity while it is open, else invalid.
    [[nodiscard]] HostWindowId id() const noexcept;

private:
    ApplicationWindowHandle window_{};
    Window* model_{};
};

// What open_native_popup_window needs. Borrowed for the call.
struct NativePopupOpening final {
    // The owner window's state in the run (SurfaceRoot::application_window_).
    std::weak_ptr<ApplicationWindowState> owner{};
    // Unique among the owner's popups for the run: the popup window's stable
    // id is the owner's followed by "/popup." and this number.
    std::uint64_t serial{};
    // A live detached root (validated by the caller).
    Control::Ptr content{};
    // In the owner's client area, logical units; each extent at least 1.
    Rect bounds{};
    HostPopupFocus focus{HostPopupFocus::passive};
    std::optional<BackdropChain> backdrop{};
    // The owner's keyboard-command keys and resources (theme, fonts,
    // images), shared with the popup.
    std::shared_ptr<KeyBindings> key_bindings{};
    std::shared_ptr<ApplicationResources> resources{};
};

// Whether the owner's run can open a native popup now: none when the owner
// is a ready window of a live run whose host opens popup windows during the
// run, else why not (no_run or unsupported).
[[nodiscard]] NativePopupRefusal native_popup_refusal(const std::weak_ptr<ApplicationWindowState>& owner) noexcept;

// Opens `opening.content` in a native popup window owned by the owner
// window: a Window model of the bounds' size, non-opaque, sharing the
// owner's keys, placed at the owner's client origin plus the bounds'
// origin and moved the least distance that keeps it inside the work area
// of the monitor under that point. Returns null when the run refuses; the
// content is then still the caller's only if it is still a detached root
// (a refusal after the native request started consumes it).
[[nodiscard]] std::shared_ptr<NativePopupRecord> open_native_popup_window(const NativePopupOpening& opening);

} // namespace gui_forms::detail
