#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/component.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/scheduler.hpp"

#include <chrono>
#include <memory>

namespace gui_forms {

class Window;

// A renderer-free timer whose callbacks are serialized with input, layout,
// and paint on the owning Window's UI thread. Late ticks are coalesced rather
// than replayed in a burst.
class GUI_FORMS_API Timer final : public Component {
public:
    explicit Timer(Window& window,
                   std::chrono::milliseconds interval =
                       std::chrono::milliseconds(100));
    ~Timer() override;

    [[nodiscard]] std::chrono::milliseconds interval() const noexcept {
        return interval_;
    }
    void set_interval(std::chrono::milliseconds interval);

    [[nodiscard]] bool enabled() const noexcept;
    void start();
    void start_at(FrameTime first_deadline);
    void stop();

    [[nodiscard]] Event<>& tick() noexcept { return tick_; }

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void verify_dispose_thread() override;
    void on_dispose() noexcept override;

private:
    struct CallbackState final {
        Timer* owner{};
    };
    struct ScheduledTickCallback final {
        std::weak_ptr<CallbackState> state;

        void operator()(FrameTime now) const;
    };

    [[nodiscard]] Window* bound_window() const noexcept;
    void require_mutable_timer(std::string_view operation) const;
    void schedule(FrameTime first_deadline);

    std::weak_ptr<detail::WindowLifetime> window_lifetime_;
    std::chrono::milliseconds interval_;
    std::shared_ptr<CallbackState> callback_state_;
    FrameRequestToken request_;
    Event<> tick_;
    bool enabled_{};
};

} // namespace gui_forms
