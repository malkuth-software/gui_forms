#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/commands/command/command.hpp"
#include "gui_forms/component/component/component.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// The value command of a colour picker (stage RB, phase 6; owner decision
// RB-13, E2-2): it holds one shared Command, whose identity and state it
// shares, and adds a typed colour, or none for "Automatic" (the program's
// default colour). The pattern is GalleryCommand's: set_value is the
// program's change (value_changed, no invocation), commit the user's choice
// (value_changed, then the Command executes); a commit is refused while the
// Command is disabled. A committed colour joins the recent colours, most
// recent first, which every picker bound to the command shows.
class GUI_FORMS_API ColorCommand final : public Component {
public:
    static constexpr std::size_t maximum_recent_colors = 10U;

    // Throws std::invalid_argument for a null command.
    explicit ColorCommand(std::shared_ptr<Command> command);

    [[nodiscard]] const std::shared_ptr<Command>& command() const noexcept { return command_; }
    [[nodiscard]] const std::string& stable_id() const noexcept { return (*command_).stable_id(); }
    [[nodiscard]] const CommandState& state() const noexcept { return (*command_).state(); }

    // The colour, or none for Automatic.
    [[nodiscard]] const std::optional<Color>& value() const noexcept { return value_; }
    void set_value(std::optional<Color> value);
    // The user chose a colour (or Automatic): sets the value (value_changed
    // when it differs), records a colour as the most recent, then executes
    // the Command. Returns false, changing nothing, while it is disabled.
    bool commit(std::optional<Color> value, std::string_view source_id);

    // Recently committed colours, most recent first, without repeats.
    [[nodiscard]] std::span<const Color> recent_colors() const& noexcept {
        return std::span<const Color>(recent_.data(), recent_.size());
    }
    std::span<const Color> recent_colors() const&& = delete;
    void clear_recent_colors() noexcept { recent_.clear(); }

    [[nodiscard]] Event<const std::optional<Color>&>& value_changed() noexcept { return value_changed_; }

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_dispose() noexcept override;

private:
    std::shared_ptr<Command> command_;
    std::optional<Color> value_{};
    std::vector<Color> recent_{};
    Event<const std::optional<Color>&> value_changed_{};
};

} // namespace gui_forms
