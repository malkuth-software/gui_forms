#pragma once

// Retained control tree for the GUI.Forms Correspondence demonstration. The
// shell is one Panel that owns the fixture model, the commands shared by the
// command bar, menu strip and context menu, and the three-pane composition.
// It consumes only public GUI.Forms headers.

#include "correspondence_model.hpp"

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/collection_controls.hpp"
#include "gui_forms/commands.hpp"
#include "gui_forms/container_controls.hpp"
#include "gui_forms/controls/panel/text_box/text_box.hpp"
#include "gui_forms/menu_controls.hpp"
#include "gui_forms/tooltip.hpp"
#include "gui_forms/window.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::correspondence {

// Command-bar item IDs. CommandBar names its retained item buttons
// "<bar id>.item.<n>", so the Archive button is
// "correspondence.command-bar.item.4".
inline constexpr int command_new_message = 1;
inline constexpr int command_reply = 2;
inline constexpr int command_forward = 3;
inline constexpr int command_archive = 4;
inline constexpr int command_delete = 5;
inline constexpr int command_flag = 6;

// Stable IDs addressed by automation and the interaction tests.
namespace ids {
inline constexpr std::string_view shell = "correspondence.shell";
inline constexpr std::string_view menu = "correspondence.menu";
inline constexpr std::string_view commands = "correspondence.commands";
inline constexpr std::string_view command_bar = "correspondence.command-bar";
inline constexpr std::string_view search = "correspondence.search";
inline constexpr std::string_view split = "correspondence.split";
inline constexpr std::string_view folders = "correspondence.folders";
inline constexpr std::string_view content_split = "correspondence.content-split";
inline constexpr std::string_view messages = "correspondence.messages";
inline constexpr std::string_view reading = "correspondence.reading";
inline constexpr std::string_view reading_subject = "correspondence.reading.subject";
inline constexpr std::string_view reading_correspondent = "correspondence.reading.correspondent";
inline constexpr std::string_view reading_date = "correspondence.reading.date";
inline constexpr std::string_view reading_body = "correspondence.reading.body";
inline constexpr std::string_view status = "correspondence.status";
inline constexpr std::string_view context_menu = "correspondence.messages.menu";
inline constexpr std::string_view folder_prefix = "correspondence.folder.";
inline constexpr std::string_view message_prefix = "correspondence.message.";
}  // namespace ids

[[nodiscard]] std::string command_button_id(int command);
[[nodiscard]] std::string folder_id(Folder folder);
[[nodiscard]] std::string message_id(std::uint32_t id);

// The palette the Gallery pages once wrote as their page style (in
// src/controls/gallery_pages.cpp); the demo invents no theme.
[[nodiscard]] BasicControlStyle correspondence_style();

class CorrespondenceShell final : public Panel {
public:
    static constexpr bool initialize_tree_after_construction = true;

    explicit CorrespondenceShell(StableId stable_id);
    void initialize_control_tree();

    [[nodiscard]] CorrespondenceModel& model() noexcept { return model_; }
    [[nodiscard]] const CorrespondenceModel& model() const noexcept { return model_; }

    // Projects the model into every retained child inside one update scope.
    // Idempotent; re-entrant calls from the controls' own change events are
    // ignored while a projection is in progress.
    void synchronize();

    // Installs hover tooltips for the command buttons and the search field.
    // Requires the shell to be attached to a Window.
    void attach_tooltips();

    [[nodiscard]] std::shared_ptr<MenuStrip> menu() const noexcept { return menu_; }
    [[nodiscard]] std::shared_ptr<CommandBar> command_bar() const noexcept { return bar_; }
    [[nodiscard]] std::shared_ptr<TextBox> search_box() const noexcept { return search_; }
    [[nodiscard]] std::shared_ptr<TreeView> folders() const noexcept { return folders_; }
    [[nodiscard]] std::shared_ptr<CorrespondenceView> messages() const noexcept {
        return messages_;
    }
    [[nodiscard]] std::shared_ptr<Label> reading_subject() const noexcept {
        return reading_subject_;
    }
    [[nodiscard]] std::shared_ptr<Label> reading_correspondent() const noexcept {
        return reading_correspondent_;
    }
    [[nodiscard]] std::shared_ptr<Label> reading_date() const noexcept { return reading_date_; }
    [[nodiscard]] std::shared_ptr<TextBox> reading_body() const noexcept { return reading_body_; }
    [[nodiscard]] std::shared_ptr<Label> status() const noexcept { return status_; }
    [[nodiscard]] std::shared_ptr<ContextMenu> context_menu() const noexcept {
        return context_menu_;
    }
    [[nodiscard]] std::span<const std::shared_ptr<Command>> commands() const noexcept {
        return commands_;
    }
    [[nodiscard]] std::string about_text() const;

private:
    struct SynchronizeGuard;

    void build_commands();
    void build_menu();
    void build_command_row();
    void build_panes();
    void project_folders();
    void project_messages();
    void project_reading_pane();
    void project_status();
    void project_commands();
    void show_about();

    void on_folder_selected(const TreeSelectionChange& change);
    void on_message_selected(const CorrespondenceSelectionChange& change);
    void on_message_activated(const std::string& stable_id);
    void on_context_requested(const ObjectContextRequest& request);
    void on_search_changed(const std::string& text);
    void on_new_message();
    void on_reply();
    void on_forward();
    void on_archive();
    void on_delete();
    void on_flag();
    void on_mark_unread();
    void on_focus_search();
    void on_about();
    void on_view_folder(Folder folder);

    CorrespondenceModel model_;
    std::vector<std::shared_ptr<Command>> commands_;
    std::shared_ptr<Command> new_message_command_;
    std::shared_ptr<Command> reply_command_;
    std::shared_ptr<Command> forward_command_;
    std::shared_ptr<Command> archive_command_;
    std::shared_ptr<Command> delete_command_;
    std::shared_ptr<Command> flag_command_;
    std::shared_ptr<Command> mark_unread_command_;
    std::shared_ptr<Command> search_command_;
    std::shared_ptr<Command> about_command_;
    std::vector<std::shared_ptr<Command>> folder_commands_;
    std::shared_ptr<MenuStrip> menu_;
    std::shared_ptr<Panel> command_row_;
    std::shared_ptr<CommandBar> bar_;
    std::shared_ptr<TextBox> search_;
    std::shared_ptr<SplitContainer> split_;
    std::shared_ptr<TreeView> folders_;
    std::shared_ptr<SplitContainer> content_split_;
    std::shared_ptr<CorrespondenceView> messages_;
    std::shared_ptr<Panel> reading_;
    std::shared_ptr<Label> reading_subject_;
    std::shared_ptr<Label> reading_correspondent_;
    std::shared_ptr<Label> reading_date_;
    std::shared_ptr<TextBox> reading_body_;
    std::shared_ptr<Label> status_;
    std::shared_ptr<ContextMenu> context_menu_;
    std::shared_ptr<ToolTip> tooltips_;
    std::uint64_t next_dialog_request_{1U};
    bool synchronizing_{};
    bool initialized_{};
};

struct CorrespondenceTree final {
    std::shared_ptr<CorrespondenceShell> root;
};

[[nodiscard]] CorrespondenceTree build_correspondence_tree();

}  // namespace gui_forms::correspondence
