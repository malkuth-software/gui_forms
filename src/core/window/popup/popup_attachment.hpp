#pragma once

#include "gui_forms/window.hpp"

namespace gui_forms::detail {

class PopupAttachment final : public Revocable {
public:
    PopupAttachment(SurfaceRoot& window, Control::Ptr owner, Control::Ptr popup)
        : window_(&window), owner_(std::move(owner)), popup_(std::move(popup)) {}

    void disconnect() noexcept override {
        if (connected_ && window_ != nullptr) {
            (*window_).close_popup(*this);
        }
    }
    [[nodiscard]] bool connected() const noexcept override { return connected_; }
    [[nodiscard]] Control::Ptr owner() const noexcept { return owner_.lock(); }
    [[nodiscard]] Control::Ptr popup() const noexcept { return popup_.lock(); }
    [[nodiscard]] Event<>& closed() noexcept { return closed_; }
    void revoke(bool publish_closed = true) noexcept {
        connected_ = false;
        window_ = nullptr;
        if (publish_closed) closed_.emit();
        owner_.reset();
        popup_.reset();
    }
    void publish_closed() { closed_.emit(); }

private:
    SurfaceRoot* window_{};
    Control::WeakPtr owner_;
    Control::WeakPtr popup_;
    bool connected_{true};
    Event<> closed_;
};

} // namespace gui_forms::detail
