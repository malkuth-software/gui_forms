#pragma once

#include "gui_forms/window.hpp"

namespace gui_forms::detail {

class AcceleratorAttachment final : public Revocable {
public:
    AcceleratorAttachment(SurfaceRoot& window, Component& owner, KeyGesture gesture,
                          std::function<bool()> callback,
                          AcceleratorOptions options)
        : window_(&window), owner_(&owner), gesture_(gesture),
          callback_(std::move(callback)), options_(options) {}

    void disconnect() noexcept override {
        if (connected_ && window_) (*window_).close_accelerator(*this);
    }
    [[nodiscard]] bool connected() const noexcept override { return connected_; }
    [[nodiscard]] Component* owner() const noexcept { return owner_; }
    [[nodiscard]] KeyGesture gesture() const noexcept { return gesture_; }
    [[nodiscard]] AcceleratorOptions options() const noexcept { return options_; }
    bool invoke() { return callback_ && callback_(); }
    void revoke() noexcept {
        connected_ = false;
        window_ = nullptr;
        owner_ = nullptr;
        callback_ = {};
    }

private:
    SurfaceRoot* window_{};
    Component* owner_{};
    KeyGesture gesture_{};
    std::function<bool()> callback_;
    AcceleratorOptions options_{};
    bool connected_{true};
};

} // namespace gui_forms::detail
