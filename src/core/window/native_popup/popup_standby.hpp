#pragma once

#include "../../application/transient_window.hpp"

#include "gui_forms/window.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace gui_forms::detail {

// A window's warm Popup windows (owner, 2026-11-08: APP_WINDOW_RULES_DESIGN
// Part S; menus are Popups). Window keeps one; slot i holds at most one
// TransientWindow of kind popup, opened hidden on the slot's first show and
// kept, hidden between shows, until the window goes or the host closes it.
// Bounds are the owner's client coordinates; the standby places them on the
// desktop through the owner's frame (desktop_placement). UI thread only.
class PopupStandby final {
public:
    explicit PopupStandby(Window& owner) noexcept : owner_(owner) {}
    PopupStandby(const PopupStandby&) = delete;
    PopupStandby& operator=(const PopupStandby&) = delete;

    [[nodiscard]] bool show(std::size_t slot, const Control::Ptr& content, Rect bounds);
    [[nodiscard]] bool move(std::size_t slot, Rect bounds);
    void hide(std::size_t slot) noexcept;
    [[nodiscard]] Window* model(std::size_t slot) const noexcept;
    [[nodiscard]] Event<std::size_t>& closed() noexcept { return closed_; }

private:
    // A slot's window closed under the standby (its owner closed, or the
    // host destroyed it); raised to closed() when it was showing.
    struct SlotClosed final {
        PopupStandby* standby{};
        std::size_t slot{};

        void operator()() const;
    };
    struct PopupSlot final {
        std::unique_ptr<TransientWindow> window{};
        SubscriptionToken closed{};
    };

    // The slot's open window, opening it hidden when it has none; null
    // when the run refuses.
    [[nodiscard]] TransientWindow* slot_window(std::size_t slot);
    void slot_closed(std::size_t slot);

    Window& owner_;
    // Slot i is menu depth i; a slot's window lives from its first show.
    std::array<PopupSlot, Window::maximum_popup_windows> slots_{};
    std::uint64_t serial_{};
    Event<std::size_t> closed_;
};

} // namespace gui_forms::detail
