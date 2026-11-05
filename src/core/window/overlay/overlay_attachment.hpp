#pragma once

#include "gui_forms/window.hpp"

namespace gui_forms::detail {

// One shown overlay (SurfaceRoot::open_overlay). The window owns the
// overlay root while it shows (owner, 2026-11-05): the attachment holds it
// from open_overlay until close_overlay or revoke, so a caller that drops
// its own handle does not make a shown overlay vanish. The owner control is
// only observed. The window's overlay list and the returned token share the
// attachment; the owner keeps a weak revocable reference to it.
class OverlayAttachment final : public Revocable {
public:
    OverlayAttachment(SurfaceRoot& window, Control::Ptr owner, Control::Ptr overlay)
        : window_(&window), owner_(std::move(owner)), overlay_(std::move(overlay)) {}

    void disconnect() noexcept override {
        if (connected_ && window_ != nullptr) {
            (*window_).close_overlay(*this);
        }
    }
    [[nodiscard]] bool connected() const noexcept override { return connected_; }
    [[nodiscard]] Control::Ptr owner() const noexcept { return owner_.lock(); }
    [[nodiscard]] Control::Ptr overlay() const noexcept { return overlay_; }
    [[nodiscard]] Event<>& closed() noexcept { return closed_; }
    void revoke(bool publish_closed = true) noexcept {
        connected_ = false;
        window_ = nullptr;
        if (publish_closed) closed_.emit();
        owner_.reset();
        overlay_.reset();
    }
    void publish_closed() { closed_.emit(); }

private:
    SurfaceRoot* window_{};
    Control::WeakPtr owner_;
    // Owned until revoke; close_overlay holds its own reference through the
    // detach, so the root leaves the window before it can be destroyed.
    Control::Ptr overlay_;
    bool connected_{true};
    Event<> closed_;
};

} // namespace gui_forms::detail
