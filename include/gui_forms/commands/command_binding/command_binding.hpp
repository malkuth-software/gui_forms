#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/commands/command/command.hpp"

#include <memory>

namespace gui_forms {

// RAII two-way binding between one command and one button presentation.
class GUI_FORMS_API CommandBinding final {
public:
    CommandBinding(std::shared_ptr<Command> command,
                   std::shared_ptr<ButtonBase> button,
                   CommandBindingOptions options = {});
    ~CommandBinding() = default;
    CommandBinding(CommandBinding&&) noexcept = default;
    CommandBinding& operator=(CommandBinding&&) noexcept = default;
    CommandBinding(const CommandBinding&) = delete;
    CommandBinding& operator=(const CommandBinding&) = delete;

    [[nodiscard]] const std::shared_ptr<Command>& command() const noexcept {
        return command_;
    }
    [[nodiscard]] const std::shared_ptr<ButtonBase>& button() const noexcept {
        return button_;
    }

private:
    void apply(const CommandState& state);

    std::shared_ptr<Command> command_;
    std::shared_ptr<ButtonBase> button_;
    CommandBindingOptions options_;
    SubscriptionToken click_;
    SubscriptionToken state_;
};

} // namespace gui_forms
