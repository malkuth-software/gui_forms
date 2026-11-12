#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/commands/command/command.hpp"
#include "gui_forms/component/component/component.hpp"
#include "gui_forms/event.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace gui_forms {

class Window;

// A preview a gallery asks the application to show (preview_requested) or to
// withdraw (preview_cancelled): the gallery item's stable ID and the stable
// ID of the gallery that asked.
struct GUI_FORMS_API GalleryPreview final {
    std::string item_id{};
    std::string source_id{};
};

// The value command of a gallery (owner decision RB-13, E2-2).
//
// Value commands are separate types that share identity and state with a
// Command: every value command holds one shared Command, so its stable ID,
// text, description, icon, enabled and visible state are the Command's, a
// button or menu row bound to the same Command shows the same state, and a
// committed value is announced through the Command's invoked event. What a
// value command adds is a typed value: here the stable ID of the chosen
// gallery item, or none. The pattern for the value commands that follow
// (spinner, combo, colour, font) is the same: hold the shared Command, keep
// one typed value, offer set_value (the program's change: value_changed, no
// invocation) and commit (the user's choice: value_changed, then the
// Command executes), and refuse a commit while the Command is disabled. Only
// the gallery command adds live preview (owner decision RB-13, E1-1).
//
// Preview protocol, on the UI thread: at most one preview is outstanding. A
// preview of another item replaces the outstanding one without a separate
// cancellation. A commit ends an outstanding preview without a cancellation
// (the committed value replaces it); cancel_preview withdraws it. A preview
// is never an undo step: the application shows it and restores on cancel.
class GUI_FORMS_API GalleryCommand final : public Component {
public:
    // Throws std::invalid_argument for a null command.
    explicit GalleryCommand(std::shared_ptr<Command> command);

    [[nodiscard]] const std::shared_ptr<Command>& command() const noexcept { return command_; }
    [[nodiscard]] const std::string& stable_id() const& noexcept { return (*command_).stable_id(); }
    const std::string& stable_id() const&& = delete;
    [[nodiscard]] const CommandState& state() const noexcept { return (*command_).state(); }

    // The chosen item's stable ID, or none.
    [[nodiscard]] const std::optional<std::string>& value() const noexcept { return value_; }
    // The program sets the value: value_changed when it differs, no
    // invocation. Throws std::invalid_argument for an empty or invalid UTF-8
    // ID.
    void set_value(std::optional<std::string> item_id);
    // The user chose an item: ends any preview without a cancellation, sets
    // the value (value_changed when it differs), then executes the Command
    // with `source_id`. Returns false, changing nothing, while the Command is
    // disabled or for an empty or invalid ID.
    bool commit(std::string item_id, std::string_view source_id);
    // As commit, routing the Command through `window`'s command chain
    // (Command::execute_routed; owner decision DK-47). A control that has a
    // window commits this way.
    bool commit_routed(std::string item_id, Window& window, std::string_view source_id);

    // The outstanding preview's item, or none.
    [[nodiscard]] const std::optional<std::string>& previewing() const noexcept { return previewing_; }
    // Asks for a preview of an item. Returns false, raising nothing, while
    // the Command is disabled, for an empty or invalid ID, or when that item
    // is already previewed.
    bool preview(std::string item_id, std::string_view source_id);
    // Withdraws the outstanding preview, if any, raising preview_cancelled.
    void cancel_preview(std::string_view source_id);

    [[nodiscard]] Event<const std::optional<std::string>&>& value_changed() noexcept { return value_changed_; }
    [[nodiscard]] Event<const GalleryPreview&>& preview_requested() noexcept { return preview_requested_; }
    [[nodiscard]] Event<const GalleryPreview&>& preview_cancelled() noexcept { return preview_cancelled_; }

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_dispose() noexcept override;

private:
    bool commit_into(std::string item_id, Window* window, std::string_view source_id);

    std::shared_ptr<Command> command_;
    std::optional<std::string> value_{};
    std::optional<std::string> previewing_{};
    Event<const std::optional<std::string>&> value_changed_{};
    Event<const GalleryPreview&> preview_requested_{};
    Event<const GalleryPreview&> preview_cancelled_{};
};

} // namespace gui_forms
