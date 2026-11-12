#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/commands/command/command.hpp"
#include "gui_forms/component.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/events.hpp"
#include "gui_forms/scheduler.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/components/ribbon_tool_tip/ribbon_tool_tip_capabilities/ribbon_tool_tip_capabilities.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gui_forms {

class Control;
class Timer;
class ToolStrip;
class Window;

// What an enhanced tooltip shows (owner decision RB-13, E3-2): a title, a
// description, the shortcut, an image, the reason a disabled command is
// unavailable, and a help topic F1 opens while the tooltip shows.
//
// With a `command`, an empty title, description, shortcut or disabled reason
// is taken from the command's state when the tooltip opens, and the command's
// enabled state says whether the target is disabled. A ToolStrip item with a
// command supplies that command when `command` is empty.
struct GUI_FORMS_API RibbonToolTipContent final {
    GUI_FORMS_LOCALIZABLE(tool_tip) std::string title{};
    GUI_FORMS_LOCALIZABLE(tool_tip) std::string description{};
    // Generated from the key binding: not translated text.
    std::string shortcut{};
    ImageId image{};
    GUI_FORMS_LOCALIZABLE(tool_tip) std::string disabled_reason{};
    std::string help_topic{};
    std::shared_ptr<Command> command{};
};

// The tooltip opened or closed. `item_id` names the ToolStrip item it
// describes, or is empty for a whole control. `content` is what it shows,
// resolved from its command; `disabled_reason` is empty unless the target
// was disabled.
struct GUI_FORMS_API RibbonToolTipEvent final {
    std::string target_stable_id{};
    std::string item_id{};
    RibbonToolTipContent content{};
    bool shown{};
    bool keyboard_initiated{};
};

// F1 was pressed while a tooltip with a help topic showed. GUI.Forms opens
// no help viewer: the application handles the topic.
struct GUI_FORMS_API RibbonToolTipHelpRequest final {
    std::string target_stable_id{};
    std::string item_id{};
    std::string help_topic{};
};

// A nonvisual provider of enhanced tooltips, on ToolTip's model: a mapping
// per control, or per item of a ToolStrip (and so of a QuickAccessToolbar),
// shown after a hover delay or at once when keyboard focus reaches the
// target, in a retained, input-transparent overlay owned by the target.
// A Ribbon can be mapped as a whole control; its commands cannot be mapped
// one by one yet.
//
// Content without a title takes the thing's plain tooltip
// (Control::tool_tip_text, or a strip item's tool_tip_text) as its title.
// While a mapping exists, its window's plain presenter shows no tooltip for
// what it describes: the control (with every part, for a control mapping)
// or the item.
class GUI_FORMS_API RibbonToolTip final : public Component, public RibbonToolTipCapabilities<RibbonToolTip> {
public:
    explicit RibbonToolTip(Window& window);
    ~RibbonToolTip() override;

    // Maps a whole control. Throws std::invalid_argument for a target that
    // is not a live control attached to this provider's Window, or content
    // that is not valid UTF-8 or has no title, no command and a target
    // without a plain tooltip.
    void set_tool_tip(const std::shared_ptr<Control>& target, RibbonToolTipContent content);
    // Maps one item of a strip; the item's command, text or plain tooltip
    // may supply the title. Throws as set_tool_tip does, and std::out_of_range for an ID
    // that names no item of the strip.
    void set_item_tool_tip(const std::shared_ptr<ToolStrip>& strip, std::string_view item_id,
                           RibbonToolTipContent content);
    [[nodiscard]] bool has_tool_tip(const Control& target, std::string_view item_id = {}) const;
    bool remove_tool_tip(const Control& target, std::string_view item_id = {});
    void clear();

    // Shows a mapping at once, anchored below its target, as keyboard focus
    // would. Throws std::invalid_argument for an unmapped target or item.
    void show(const std::shared_ptr<Control>& target, std::string_view item_id = {});
    void hide();
    [[nodiscard]] bool visible() const noexcept;
    [[nodiscard]] std::shared_ptr<Control> active_control() const noexcept;
    [[nodiscard]] const std::string& active_item_id() const& noexcept { return visible_item_; }
    const std::string& active_item_id() const&& = delete;
    // What the visible tooltip shows (empty when none is visible).
    [[nodiscard]] const RibbonToolTipContent& shown_content() const noexcept { return shown_content_; }
    // The overlay's bounds in window coordinates (empty when none).
    [[nodiscard]] Rect overlay_bounds() const;

    [[nodiscard]] Event<const RibbonToolTipEvent&>& visibility_changed() noexcept {
        return visibility_changed_;
    }
    [[nodiscard]] Event<const RibbonToolTipHelpRequest&>& help_requested() noexcept {
        return help_requested_;
    }

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void verify_dispose_thread() override;
    void on_dispose() noexcept override;

private:
    struct ItemMapping final {
        std::string item_id;
        RibbonToolTipContent content;
    };
    struct Entry;
    struct TargetPointerObserver;
    struct TargetFocusObserver;
    struct StripFocusObserver;
    struct OverlayRevocationObserver;
    struct HelpAccelerator final {
        RibbonToolTip* provider{};

        bool operator()() const;
    };
    class OverlayHolder;
    class AcceleratorHolder;
    using EntryMap = std::unordered_map<std::uint64_t, std::unique_ptr<Entry>>;
    enum class PendingAction : std::uint8_t { none, show, auto_hide };

    [[nodiscard]] Window* bound_window() const noexcept;
    void require_access(std::string_view operation) const;
    [[nodiscard]] Entry* find_entry(const Control& target);
    [[nodiscard]] const Entry* find_entry(const Control& target) const;
    [[nodiscard]] Entry& require_entry(const std::shared_ptr<Control>& target);
    [[nodiscard]] const RibbonToolTipContent* find_content(const Entry& entry, std::string_view item_id) const;
    [[nodiscard]] RibbonToolTipContent resolve(const Control& target, std::string_view item_id,
                                               const RibbonToolTipContent& authored) const;
    [[nodiscard]] Rect anchor_bounds(const Control& target, std::string_view item_id) const;
    // The plain tooltip of `target` or of its part `item_id`, from the
    // control or its semantic node.
    [[nodiscard]] std::string plain_tool_tip(const Control& target, std::string_view item_id) const;
    // While a mapping exists, the window's plain presenter shows nothing for
    // what it covers (Window::cover_tool_tip).
    void set_cover(const Control& target, std::string_view item_id, bool covered);
    void uncover_all() noexcept;
    void target_pointer(const std::shared_ptr<Control>& target, const PointerEvent& event);
    void target_focus(const std::shared_ptr<Control>& target, bool focused);
    void strip_focus(const std::shared_ptr<Control>& target, const std::string& item_id);
    void schedule_show(const std::shared_ptr<Control>& target, std::string_view item_id,
                       bool keyboard_initiated);
    void arm(std::chrono::milliseconds delay, PendingAction action);
    void timer_tick();
    void show_now(const std::shared_ptr<Control>& target, const std::string& item_id, bool keyboard_initiated);
    void close_overlay(bool emit_change);
    void overlay_revoked();
    bool request_help();

    std::weak_ptr<detail::WindowLifetime> window_lifetime_;
    std::unique_ptr<Timer> timer_;
    EntryMap entries_;
    std::weak_ptr<Control> pending_target_;
    std::string pending_item_;
    std::weak_ptr<Control> visible_target_;
    std::string visible_item_;
    RibbonToolTipContent shown_content_;
    std::shared_ptr<Control> overlay_layer_;
    std::shared_ptr<Control> overlay_bubble_;
    std::unique_ptr<OverlayHolder> overlay_;
    std::unique_ptr<AcceleratorHolder> accelerator_;
    SubscriptionToken timer_subscription_;
    SubscriptionToken overlay_subscription_;
    Event<const RibbonToolTipEvent&> visibility_changed_;
    Event<const RibbonToolTipHelpRequest&> help_requested_;
    PendingAction pending_action_{PendingAction::none};
    bool keyboard_initiated_{};
    std::uint64_t provider_id_{};
    // property_codemod begin RibbonToolTip (tools/property_families/ribbon_tool_tip.json; generated, do not edit)
private:
    friend struct RibbonToolTipInitialDelayCapability<RibbonToolTip>;
    friend struct RibbonToolTipAutoPopDelayCapability<RibbonToolTip>;
    friend struct RibbonToolTipHelpLineTextCapability<RibbonToolTip>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(initial_delay)& cell, std::chrono::milliseconds& incoming);
    void written(const decltype(initial_delay)& cell);
    void prepare_write(const decltype(auto_pop_delay)& cell, std::chrono::milliseconds& incoming);
    void written(const decltype(auto_pop_delay)& cell);
    void prepare_write(const decltype(help_line_text)& cell, std::string& incoming);
    void written(const decltype(help_line_text)& cell);
    // property_codemod end RibbonToolTip
};

} // namespace gui_forms
