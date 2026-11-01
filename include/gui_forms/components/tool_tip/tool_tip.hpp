#pragma once

#include "gui_forms/component.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/events.hpp"
#include "gui_forms/scheduler.hpp"
#include "gui_forms/window/tool_tips/tool_tips.hpp"

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace gui_forms {

class Control;
class Window;

// The Windows Forms way to set tooltip text and timing (owner, 2026-10-28).
// A ToolTip shows nothing itself: tooltips belong to the things they
// describe, and each window has one presenter (see ToolTipSettings).
// set_tool_tip writes the target's Control::tool_tip_text; the delays, the
// width and the policies are the window's ToolTipSettings, which every
// ToolTip of the window shares, so the last one set holds.
// visibility_changed reports the window's tooltips for the targets this
// ToolTip set. Disposing a ToolTip clears the text it set. UI thread only;
// every operation throws std::logic_error once the ToolTip is disposed or
// its window is gone.
class ToolTip final : public Component {
public:
    explicit ToolTip(Window& window);
    ~ToolTip() override;

    // Sets `target`'s tooltip text; empty text removes it. Throws
    // std::invalid_argument for a target that is not a live control attached
    // to this ToolTip's window, or text that is not valid UTF-8.
    void set_tool_tip(const std::shared_ptr<Control>& target, std::string text);
    // `target`'s tooltip text, whoever set it.
    [[nodiscard]] std::string tool_tip(const Control& target) const;
    // Clears the text this ToolTip set on `target`: false, changing nothing,
    // when it set none there.
    bool remove_tool_tip(Control& target);
    // Clears the text this ToolTip set on every target.
    void clear();

    // The window's ToolTipSettings, read and written field by field. The
    // setters throw std::invalid_argument for a negative delay and for a
    // width outside 80 to 2048, changing nothing.
    [[nodiscard]] std::chrono::milliseconds initial_delay() const;
    void set_initial_delay(std::chrono::milliseconds delay);
    [[nodiscard]] std::chrono::milliseconds reshow_delay() const;
    void set_reshow_delay(std::chrono::milliseconds delay);
    [[nodiscard]] std::chrono::milliseconds auto_pop_delay() const;
    void set_auto_pop_delay(std::chrono::milliseconds delay);
    [[nodiscard]] bool show_on_hover() const;
    void set_show_on_hover(bool value);
    [[nodiscard]] bool show_on_focus() const;
    void set_show_on_focus(bool value);
    // Whether a disabled control's tooltip shows
    // (ToolTipSettings::show_when_disabled).
    [[nodiscard]] bool show_always() const;
    void set_show_always(bool value);
    [[nodiscard]] double maximum_width() const;
    void set_maximum_width(double width);

    // Shows `target`'s tooltip at once, below it (Window::show_tool_tip),
    // for the auto-pop delay or for `duration` (zero: until hidden). Throws
    // std::invalid_argument for a target this ToolTip set no text on.
    void show(const std::shared_ptr<Control>& target);
    void show(const std::shared_ptr<Control>& target, std::chrono::milliseconds duration);
    // Closes the window's tooltip when it shows one of this ToolTip's
    // targets.
    void hide();
    [[nodiscard]] bool visible() const;
    [[nodiscard]] std::shared_ptr<Control> active_control() const;

    [[nodiscard]] Event<const ToolTipEvent&>& visibility_changed() noexcept {
        return visibility_changed_;
    }

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void verify_dispose_thread() override;
    void on_dispose() noexcept override;

private:
    // The targets this ToolTip set text on, by stable ID (the window's
    // visibility event names targets by stable ID). Observed, never owned.
    using TargetMap = std::map<std::string, std::weak_ptr<Control>, std::less<>>;

    [[nodiscard]] Window* bound_window() const noexcept;
    [[nodiscard]] Window& require_access(std::string_view operation) const;
    [[nodiscard]] bool owns(const Control& target) const;
    void window_tool_tip_changed(const ToolTipEvent& change);

    std::weak_ptr<detail::WindowLifetime> window_lifetime_;
    TargetMap targets_;
    SubscriptionToken window_subscription_;
    Event<const ToolTipEvent&> visibility_changed_;
};

} // namespace gui_forms
