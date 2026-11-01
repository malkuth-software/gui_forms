#pragma once

#include "gui_forms/component.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/events.hpp"
#include "gui_forms/scheduler.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace gui_forms {

class Control;
class Timer;
class Window;

struct ToolTipEvent final {
    std::string target_stable_id;
    std::string text;
    bool shown{};
    bool keyboard_initiated{};
};

// A nonvisual provider. Each mapping is tokenized to the target Control and
// presents a retained, input-transparent overlay owned by that target.
class ToolTip final : public Component {
public:
    explicit ToolTip(Window& window);
    ~ToolTip() override;

    void set_tool_tip(const std::shared_ptr<Control>& target, GUI_FORMS_LOCALIZABLE(tool_tip) std::string text);
    [[nodiscard]] std::string tool_tip(const Control& target) const;
    bool remove_tool_tip(const Control& target);
    void clear();

    [[nodiscard]] std::chrono::milliseconds initial_delay() const noexcept {
        return initial_delay_;
    }
    void set_initial_delay(std::chrono::milliseconds delay);
    [[nodiscard]] std::chrono::milliseconds reshow_delay() const noexcept {
        return reshow_delay_;
    }
    void set_reshow_delay(std::chrono::milliseconds delay);
    [[nodiscard]] std::chrono::milliseconds auto_pop_delay() const noexcept {
        return auto_pop_delay_;
    }
    void set_auto_pop_delay(std::chrono::milliseconds delay);
    [[nodiscard]] bool show_on_hover() const noexcept { return show_on_hover_; }
    void set_show_on_hover(bool value);
    [[nodiscard]] bool show_on_focus() const noexcept { return show_on_focus_; }
    void set_show_on_focus(bool value);
    [[nodiscard]] bool show_always() const noexcept { return show_always_; }
    void set_show_always(bool value);
    [[nodiscard]] double maximum_width() const noexcept {
        return maximum_width_;
    }
    void set_maximum_width(double width);

    void show(const std::shared_ptr<Control>& target);
    void show(const std::shared_ptr<Control>& target,
              std::chrono::milliseconds duration);
    void hide();
    [[nodiscard]] bool visible() const noexcept;
    [[nodiscard]] std::shared_ptr<Control> active_control() const noexcept;

    [[nodiscard]] Event<const ToolTipEvent&>& visibility_changed() noexcept {
        return visibility_changed_;
    }

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void verify_dispose_thread() override;
    void on_dispose() noexcept override;

private:
    struct Entry;
    struct TargetPointerObserver;
    struct TargetFocusObserver;
    struct TargetBoundsObserver;
    struct PopupRevocationObserver;
    using EntryMap =
        std::unordered_map<std::uint64_t, std::unique_ptr<Entry>>;
    enum class PendingAction : std::uint8_t { none, show, auto_hide };

    [[nodiscard]] Window* bound_window() const noexcept;
    void require_access(std::string_view operation) const;
    [[nodiscard]] Entry* find_entry(const Control& target);
    [[nodiscard]] const Entry* find_entry(const Control& target) const;
    void target_pointer(const std::shared_ptr<Control>& target,
                        const PointerEvent& event);
    void target_focus(const std::shared_ptr<Control>& target, bool focused);
    void target_moved(const std::shared_ptr<Control>& target);
    void schedule_show(const std::shared_ptr<Control>& target,
                       bool keyboard_initiated, Point pointer_position);
    void arm(std::chrono::milliseconds delay, PendingAction action);
    void timer_tick();
    void show_now(const std::shared_ptr<Control>& target,
                  bool keyboard_initiated,
                  std::optional<std::chrono::milliseconds> duration);
    void position_overlay();
    void close_overlay(bool emit_change);
    void popup_revoked();

    std::weak_ptr<detail::WindowLifetime> window_lifetime_;
    std::unique_ptr<Timer> timer_;
    EntryMap entries_;
    std::weak_ptr<Control> pending_target_;
    std::weak_ptr<Control> visible_target_;
    std::shared_ptr<Control> overlay_layer_;
    std::shared_ptr<Control> overlay_bubble_;
    class PopupHolder;
    std::unique_ptr<PopupHolder> popup_;
    SubscriptionToken timer_subscription_;
    SubscriptionToken popup_subscription_;
    Event<const ToolTipEvent&> visibility_changed_;
    std::chrono::milliseconds initial_delay_{500};
    std::chrono::milliseconds reshow_delay_{100};
    std::chrono::milliseconds auto_pop_delay_{5000};
    FrameTime last_hidden_{};
    Point pointer_position_{};
    PendingAction pending_action_{PendingAction::none};
    bool keyboard_initiated_{};
    bool anchor_to_target_{};
    bool show_on_hover_{true};
    bool show_on_focus_{true};
    bool show_always_{};
    double maximum_width_{336.0};
    std::uint64_t provider_id_{};
};

} // namespace gui_forms
