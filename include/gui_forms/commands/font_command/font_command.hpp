#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/commands/command/command.hpp"
#include "gui_forms/component/component/component.hpp"
#include "gui_forms/event.hpp"

#include <string>
#include <string_view>

namespace gui_forms {

// A font as a font control shows and chooses it: a family name and a size
// in points. An empty family or a zero size is unknown, as for a selection
// whose text mixes fonts; the control shows that field empty.
struct GUI_FORMS_API FontChoice final {
    std::string family{};
    double size{};

    friend bool operator==(const FontChoice& left, const FontChoice& right) noexcept {
        return left.family == right.family && left.size == right.size;
    }
};

// The value command of a font control (stage RB, phase 6; owner decision
// RB-13, E2-2): it holds one shared Command, so its identity, text,
// description, icon, enabled and visible state are the Command's, and it adds
// a typed FontChoice. The pattern is GalleryCommand's: set_value is the
// program's change (value_changed, no invocation), commit the user's choice
// (value_changed, then the Command executes with the source's ID); a commit
// is refused while the Command is disabled. No live preview (decision 13).
class GUI_FORMS_API FontCommand final : public Component {
public:
    // The largest size accepted, in points.
    static constexpr double maximum_size = 1638.0;

    // Throws std::invalid_argument for a null command.
    explicit FontCommand(std::shared_ptr<Command> command);

    [[nodiscard]] const std::shared_ptr<Command>& command() const noexcept { return command_; }
    [[nodiscard]] const std::string& stable_id() const noexcept { return (*command_).stable_id(); }
    [[nodiscard]] const CommandState& state() const noexcept { return (*command_).state(); }

    [[nodiscard]] const FontChoice& value() const noexcept { return value_; }
    // Whether set_value accepts `value`: valid single-line UTF-8 and a size
    // of 0 to maximum_size.
    [[nodiscard]] static bool valid_value(const FontChoice& value) noexcept;
    // Throws std::invalid_argument for invalid UTF-8, a family with a line
    // break, or a size that is negative, not finite or above maximum_size.
    void set_value(FontChoice value);
    // The user chose a font: sets the value (value_changed when it differs),
    // then executes the Command. Returns false, changing nothing, while the
    // Command is disabled, for a value set_value would refuse, or for a value
    // with neither a family nor a size.
    bool commit(FontChoice value, std::string_view source_id);

    [[nodiscard]] Event<const FontChoice&>& value_changed() noexcept { return value_changed_; }

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_dispose() noexcept override;

private:
    std::shared_ptr<Command> command_;
    FontChoice value_{};
    Event<const FontChoice&> value_changed_{};
};

} // namespace gui_forms
