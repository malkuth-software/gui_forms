#pragma once

#include "gui_forms/application/application.hpp"
#include "gui_forms/window/native_popup/native_popup.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace gui_forms::detail {

struct ApplicationWindowState;

// One native popup window (Window::open_native_popup): the run's
// handle on it and a borrowed pointer to its model. Shared by the token
// that keeps it open and, as a revocable, by the control that opened it.
// The window and its model are owned by the host's entry; the model
// pointer is used only while the handle is active, because the host
// destroys the model only after the window's state is revoked.
class NativePopupRecord final : public Revocable {
public:
    // Unbound until the run opens the window (bind).
    explicit NativePopupRecord(Window* model) noexcept;

    // The popup window's handle, once open_window returned it.
    void bind(ApplicationWindowHandle window) noexcept;
    // The popup window closed (its closed callback): the record no longer
    // names it, and closed() is raised once.
    void notify_closed();
    // Raised once, on the UI thread, when the popup window closes, whatever
    // closed it (the token, the opener's death, the host).
    [[nodiscard]] Event<>& closed() noexcept { return closed_; }

    // Destroys the popup window if it is still open (ApplicationWindows::
    // destroy_window): its closed callback runs before this returns.
    void disconnect() noexcept override;
    [[nodiscard]] bool connected() const noexcept override;
    // The popup's model while the window is open, else null.
    [[nodiscard]] Window* model() const noexcept;
    // The popup window's identity while it is open, else invalid.
    [[nodiscard]] HostWindowId id() const noexcept;
    // The run's handle on the popup window (inactive before bind and once
    // the run ended); its requests report after_shutdown when it is gone.
    [[nodiscard]] const ApplicationWindowHandle& handle() const noexcept { return window_; }

private:
    ApplicationWindowHandle window_{};
    Window* model_{};
    Event<> closed_{};
    // Set by notify_closed; the handle may still read active inside the
    // window's own closed callback.
    bool window_closed_{};
};

// The popup window's closed callback (ApplicationWindowOptions::closed):
// tells the record, if it is still alive.
struct NativePopupClosed final {
    std::weak_ptr<NativePopupRecord> record{};
    void operator()() const;
};

// What open_native_popup_window needs. Borrowed for the call.
struct NativePopupOpening final {
    // The owner window's state in the run (SurfaceRoot::application_window_).
    std::weak_ptr<ApplicationWindowState> owner{};
    // Unique among the owner's popups of this name for the run: the popup
    // window's stable id is the owner's followed by "/", `name`, "." and
    // this number.
    std::uint64_t serial{};
    std::string_view name{"popup"};
    // A live detached root (validated by the caller).
    Control::Ptr content{};
    // In the owner's client area, logical units; each extent at least 1.
    Rect bounds{};
    HostPopupFocus focus{HostPopupFocus::passive};
    // What the window is for; a Splash kind needs a passive focus.
    HostTransientKind kind{HostTransientKind::popup};
    // A warm transient window (TransientWindow) opens hidden.
    bool initially_visible{true};
    std::optional<BackdropChain> backdrop{};
    // The content draws every surface the window shows: the popup model's
    // backplane is clear over its transparent grant
    // (SurfaceRoot::declare_content_surface). Warm transient windows set it.
    bool content_surface{};
    // The owner's keyboard-command keys and resources (theme, fonts,
    // images), shared with the popup.
    std::shared_ptr<KeyBindings> key_bindings{};
    std::shared_ptr<ApplicationResources> resources{};
};

// Whether the owner's run can open a native popup of `kind` now: none when
// the owner is a ready window of a live run whose host opens popup windows
// (and, for a Splash kind, Splash windows) during the run, else why not
// (no_run or unsupported).
[[nodiscard]] NativePopupRefusal native_popup_refusal(const std::weak_ptr<ApplicationWindowState>& owner,
                                                      HostTransientKind kind = HostTransientKind::popup) noexcept;

// The work area of the monitor under `client_point` (the owner's client
// coordinates), in the owner's client coordinates; none when the owner is
// not a ready window of a live run, its frame is not reported or the host
// lists no monitor. `monitors` is the caller's storage for the table.
[[nodiscard]] std::optional<Rect> client_work_area(const std::weak_ptr<ApplicationWindowState>& owner,
                                                   Point client_point, HostMonitorResult& monitors);

// Whether the owner's popup and Splash windows show translucent now, so
// that content shown in them may cast its shadow into clear pixels around
// it: the run's host draws them translucent
// (HostWindowing::transient_windows_translucent) and a popup of the owner
// would be granted the transparent backdrop under the owner's compositor
// state and presentation settings (high contrast and reduced transparency
// withdraw it). True when there is no live run (the content opens as an
// overlay, which casts its shadow into the owner); false when popups are
// opaque, where the content is shown alone with no shadow or fade.
// `model` is the owner window's model.
[[nodiscard]] bool native_popup_translucent(const std::weak_ptr<ApplicationWindowState>& owner,
                                            const SurfaceRoot& model) noexcept;

// `bounds` in the owner's client area as a desktop rectangle in logical
// units, moved the least distance that keeps it inside the work area of
// the monitor under its origin. None when the owner is not a ready window
// of a live run or the host does not report its frame. `monitors` is the
// caller's storage for the monitor table, reused so that a warm show
// allocates nothing; it holds the last query's result after.
[[nodiscard]] std::optional<Rect> desktop_placement(const std::weak_ptr<ApplicationWindowState>& owner,
                                                    Rect bounds, HostMonitorResult& monitors);

// Opens `opening.content` in a native popup window owned by the owner
// window: a Window model of the bounds' size, non-opaque, sharing the
// owner's keys, placed at the owner's client origin plus the bounds'
// origin and moved the least distance that keeps it inside the work area
// of the monitor under that point. Returns null when the run refuses; the
// content is then still the caller's only if it is still a detached root
// (a refusal after the native request started consumes it).
[[nodiscard]] std::shared_ptr<NativePopupRecord> open_native_popup_window(const NativePopupOpening& opening);

} // namespace gui_forms::detail
