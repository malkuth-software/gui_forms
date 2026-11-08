#pragma once

#include "native_popup_record.hpp"

#include <memory>

namespace gui_forms::detail {

// What TransientWindow::open needs. Borrowed for the call.
struct TransientWindowOpening final {
    // The owner window's state in the run (SurfaceRoot::application_window_).
    std::weak_ptr<ApplicationWindowState> owner{};
    HostTransientKind kind{HostTransientKind::tool_tip};
    // Unique among the owner's transient windows of this kind for the run.
    std::uint64_t serial{};
    // The owner's keyboard-command keys and resources (theme, fonts,
    // images), shared with the transient window's model.
    std::shared_ptr<KeyBindings> key_bindings{};
    std::shared_ptr<ApplicationResources> resources{};
};

// A warm transient window (owner, 2026-11-08: APP_WINDOW_RULES_DESIGN Part S,
// SPLASH_STUDY D1): one popup-kind host window of a Splash or Popup kind,
// opened hidden once and kept for its owner's life, presented through the
// host's ordinary window path. Its small Window model holds one content
// subtree at a time: show puts the content into the model's root (unless
// it is there already), frames the host window and shows it; hide hides
// the window and leaves the content in it, so that showing the same
// content again attaches nothing (the warm show: no host window, no model,
// no attachment). Showing other content takes the old content out first.
// The content stays the caller's: it is a child of the root until other
// content replaces it or this object goes.
//
// The host window closes when this object is destroyed, when its owner
// window closes, or when the host destroys it; closed() is raised once
// then, and every later show is refused. UI thread only.
class TransientWindow final {
public:
    // Opens the window hidden, at 1 x 1; null when the run refuses (no run,
    // a host without windows of this kind, or a host refusal).
    [[nodiscard]] static std::unique_ptr<TransientWindow> open(const TransientWindowOpening& opening);

    ~TransientWindow();
    TransientWindow(const TransientWindow&) = delete;
    TransientWindow& operator=(const TransientWindow&) = delete;

    // Shows `content` (a live control without a parent or window) as the
    // whole client area at `desktop` (desktop logical units; each extent at
    // least 1), replacing content already shown. False when the window has
    // closed or the host refused the frame or the show; the content is then
    // detached again and nothing shows.
    [[nodiscard]] bool show(const Control::Ptr& content, Rect desktop);
    // Moves and resizes the window while it shows; false when nothing shows
    // or the host refused.
    [[nodiscard]] bool move(Rect desktop);
    // Hides the window, leaving its content attached; nothing when hidden.
    void hide() noexcept;
    // Takes the content out, hiding the window first when it shows.
    void clear() noexcept;

    [[nodiscard]] bool connected() const noexcept;
    [[nodiscard]] bool shown() const noexcept { return shown_; }
    // The window's model while it is open, else null.
    [[nodiscard]] Window* model() const noexcept;
    [[nodiscard]] HostWindowId id() const noexcept;
    // Raised once when the host window closes, whatever closed it.
    [[nodiscard]] Event<>& closed() noexcept { return (*record_).closed(); }

private:
    TransientWindow(std::shared_ptr<NativePopupRecord> record, Control::Ptr root) noexcept;
    // Takes the content out of the root, leaving the window as it is.
    void detach_content() noexcept;

    std::shared_ptr<NativePopupRecord> record_;
    // The model's root (owned by the model; kept to add and remove content).
    Control::Ptr root_;
    Control::Ptr content_{};
    bool shown_{};
};

} // namespace gui_forms::detail
