#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/commands/types/command_types.hpp"
#include "gui_forms/component/component/component.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"

#include <string>
#include <string_view>

namespace gui_forms {

class Window;
class SurfaceRoot;
struct KeyGesture;
class AcceleratorToken;

// Shared authority for menu, shortcut, context, semantic, and test surfaces.
class GUI_FORMS_API Command final : public Component {
public:
    Command();
    Command(std::string stable_id, std::string text);
    void clear_checked();
    [[nodiscard]] AcceleratorToken bind_shortcut(Window& window, KeyGesture gesture);

    [[nodiscard]] const std::string& stable_id() const noexcept {
        return stable_id_;
    }
    [[nodiscard]] const CommandState& state() const noexcept { return state_; }
    void set_text(GUI_FORMS_LOCALIZABLE(text) std::string text);
    void set_description(std::string description);
    void set_icon_id(std::string icon_id);
    void set_shortcut(std::string shortcut);
    void set_mnemonic(std::string mnemonic);
    void set_availability_reason(std::string reason);
    void set_enabled(bool enabled);
    void set_visible(bool visible);
    void set_checked(bool checked);
    void set_default_action(bool is_default);
    void set_destructive(bool destructive);
    bool execute(std::string_view source_id = {});
    // Runs the command through the command routing chain of `window` (a
    // Window or a sub-surface; see gui_forms/commands/command_route): the
    // first command target with a handler for this command's stable_id runs
    // it; when none has one,
    // `invoked` is raised exactly as execute() raises it. Each call takes
    // the next invocation sequence, as execute() does. Returns false,
    // running nothing, when the command is disposed or disabled, `source_id`
    // is not valid UTF-8, or the window refused the routing (nested too
    // deeply); true otherwise. Menus, the ribbon, tool strips and shortcuts
    // call this.
    bool execute_routed(SurfaceRoot& window, std::string_view source_id = {});

    [[nodiscard]] Event<const CommandState&>& state_changed() noexcept {
        return state_changed_;
    }
    [[nodiscard]] Event<const CommandInvocation&>& invoked() noexcept {
        return invoked_;
    }

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;

private:
    void publish_state();
    void on_dispose() noexcept override;

    std::string stable_id_;
    CommandState state_;
    std::uint64_t next_sequence_{1U};
    Event<const CommandState&> state_changed_;
    Event<const CommandInvocation&> invoked_;
};

} // namespace gui_forms
