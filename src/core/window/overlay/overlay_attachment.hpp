#pragma once

#include "gui_forms/window.hpp"

namespace gui_forms::detail {

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
    [[nodiscard]] Control::Ptr overlay() const noexcept { return overlay_.lock(); }
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
    Control::WeakPtr overlay_;
    bool connected_{true};
    Event<> closed_;
};

} // namespace gui_forms::detail
