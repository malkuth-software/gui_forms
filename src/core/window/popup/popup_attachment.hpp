#pragma once

#include "../backdrop/backdrop_cache.hpp"

#include "gui_forms/window.hpp"

#include <optional>
#include <vector>

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

    // The popup's backdrop (PopupOptions::backdrop) and its cache, with the
    // strips scratch of its computation; owned here, used by the window's
    // paint on the UI thread, released with the attachment.
    std::optional<BackdropChain> backdrop_chain{};
    BackdropCache backdrop_cache{};
    std::vector<BackdropStrip> backdrop_missing{};
    std::vector<BackdropStrip> backdrop_strips{};

private:
    SurfaceRoot* window_{};
    Control::WeakPtr owner_;
    Control::WeakPtr popup_;
    bool connected_{true};
    Event<> closed_;
};

} // namespace gui_forms::detail
